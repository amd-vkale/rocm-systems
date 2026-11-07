------------------------------ MODULE MultiRankReplay ------------------------------
(***************************************************************************)
(* Range replay across MPI ranks.                                          *)
(*                                                                         *)
(* Every rank runs the same program: one range whose body issues a host    *)
(* collective (IR), then a collective after the range (POST). The SDK's    *)
(* decision points are modeled as in the code:                             *)
(*                                                                         *)
(*  packet mode (executor.cpp execute_range): end() unregisters the range  *)
(*   (take_range) before the commit callback, so no other thread can       *)
(*   publish a decline afterwards; the replayed passes re-submit GPU       *)
(*   packets only and never re-run host code, so they issue no host        *)
(*   collectives.                                                          *)
(*                                                                         *)
(*  host mode (host_replay.cpp finish_pass): pass 0 ends, local checks     *)
(*   run, the commit callback runs with the lifetime lock released, then   *)
(*   fold_external_decline / allocations_unchanged are re-checked; before  *)
(*   every pass restore_entry_state re-checks; after every replayed pass   *)
(*   the continue decision is followed by the same re-checks. Each         *)
(*   re-check can decline if another thread of the process (Interference)  *)
(*   dispatched, copied, unloaded or (de)allocated in the meantime.        *)
(*                                                                         *)
(* The tool's commit callback either allreduces the vote (Agree) or        *)
(* answers locally. AbortOnBroken models a tool that, at CLOSE, calls       *)
(* MPI_Abort when its range ran fewer passes than the ranks agreed on.     *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS Ranks,          \* e.g. {1, 2, 3}
          P,              \* passes per range including the application's own (pass 0), >= 2
          Mode,           \* "packet" or "host"
          Agree,          \* tool allreduces the commit vote
          AbortOnBroken,  \* tool aborts the job when its rank lost an agreed replay
          PreDecline,     \* a rank may decline before the commit (asymmetric eligibility)
          Interference    \* other threads may cause a decline at any re-check

ASSUME P >= 2 /\ Mode \in {"packet", "host"}

VARIABLES pc,         \* per-rank program counter
          eligible,   \* per-rank local eligibility of the range
          commitRes,  \* per-rank answer of the commit callback: "none", "yes", "no"
          pass,       \* per-rank current replayed pass (host mode)
          passesRun,  \* per-rank replayed passes completed (what PASS callbacks report)
          restores,   \* per-rank restores performed
          commits,    \* per-rank commit callback invocations
          closes,     \* per-rank CLOSE callbacks
          coll,       \* per-rank sequence of collective tags issued
          waitTag,    \* per-rank tag of the collective it is blocked in, or "none"
          mismatch,   \* a collective completed with ranks in different calls
          aborted     \* the job was aborted by a tool

vars == <<pc, eligible, commitRes, pass, passesRun, restores, commits, closes, coll,
          waitTag, mismatch, aborted>>

PCs == {"body0", "end0", "commit", "postcommit", "restore", "body", "endp", "gpu",
        "close", "post", "done"}

TypeOK ==
    /\ pc \in [Ranks -> PCs]
    /\ eligible \in [Ranks -> BOOLEAN]
    /\ commitRes \in [Ranks -> {"none", "yes", "no"}]
    /\ pass \in [Ranks -> 0..P]
    /\ passesRun \in [Ranks -> 0..P]
    /\ mismatch \in BOOLEAN
    /\ aborted \in BOOLEAN

Init ==
    /\ pc = [r \in Ranks |-> "body0"]
    /\ eligible = [r \in Ranks |-> TRUE]
    /\ commitRes = [r \in Ranks |-> "none"]
    /\ pass = [r \in Ranks |-> 0]
    /\ passesRun = [r \in Ranks |-> 0]
    /\ restores = [r \in Ranks |-> 0]
    /\ commits = [r \in Ranks |-> 0]
    /\ closes = [r \in Ranks |-> 0]
    /\ coll = [r \in Ranks |-> <<>>]
    /\ waitTag = [r \in Ranks |-> "none"]
    /\ mismatch = FALSE
    /\ aborted = FALSE

Running(r) == ~aborted /\ waitTag[r] = "none"

\* A rank enters a collective and blocks; it resumes at `next` once all ranks are in one.
Enter(r, tag, next) ==
    /\ coll' = [coll EXCEPT ![r] = Append(@, tag)]
    /\ waitTag' = [waitTag EXCEPT ![r] = tag]
    /\ pc' = [pc EXCEPT ![r] = next]

\* MPI matches the k-th collective of every rank. It completes only when every rank is blocked
\* in one; if the ranks are in different calls the job is broken (MPI_ERR_TRUNCATE, or a silent
\* mismatched reduction when the calls happen to have the same signature).
CollectiveCompletes ==
    /\ ~aborted
    /\ \A r \in Ranks : waitTag[r] # "none"
    /\ LET tags == {waitTag[r] : r \in Ranks} IN
         /\ mismatch' = (mismatch \/ Cardinality(tags) > 1)
         /\ IF tags = {"COMMIT"}
              THEN commitRes' = [r \in Ranks |->
                                   IF \A q \in Ranks : eligible[q] THEN "yes" ELSE "no"]
              ELSE UNCHANGED commitRes
    /\ waitTag' = [r \in Ranks |-> "none"]
    /\ UNCHANGED <<pc, eligible, pass, passesRun, restores, commits, closes, coll, aborted>>

MaybeDecline(r) ==
    IF Interference THEN eligible' \in {[eligible EXCEPT ![r] = FALSE], eligible}
                    ELSE UNCHANGED eligible

\* Pass 0: the application's own execution, with its host collective inside the range.
Body0(r) ==
    /\ Running(r) /\ pc[r] = "body0"
    /\ Enter(r, "IR", "end0")
    /\ UNCHANGED <<eligible, commitRes, pass, passesRun, restores, commits, closes, mismatch, aborted>>

\* end(): every check that can decline before the commit has run.
End0(r) ==
    /\ Running(r) /\ pc[r] = "end0"
    /\ IF PreDecline THEN eligible' \in {[eligible EXCEPT ![r] = FALSE], eligible}
                     ELSE UNCHANGED eligible
    /\ pc' = [pc EXCEPT ![r] = "commit"]
    /\ UNCHANGED <<commitRes, pass, passesRun, restores, commits, closes, coll, waitTag, mismatch, aborted>>

\* The commit callback: delivered once on every outcome (declined ranges get it from close).
Commit(r) ==
    /\ Running(r) /\ pc[r] = "commit"
    /\ commits' = [commits EXCEPT ![r] = @ + 1]
    /\ LET next == IF Mode = "host" THEN "postcommit" ELSE "gpu" IN
       IF Agree
         THEN /\ Enter(r, "COMMIT", next)
              /\ UNCHANGED commitRes
         ELSE /\ commitRes' = [commitRes EXCEPT ![r] = IF eligible[r] THEN "yes" ELSE "no"]
              /\ pc' = [pc EXCEPT ![r] = next]
              /\ UNCHANGED <<coll, waitTag>>
    /\ UNCHANGED <<eligible, pass, passesRun, restores, closes, mismatch, aborted>>

\* Host mode, back from the callback: fold_external_decline + allocations_unchanged.
PostCommit(r) ==
    /\ Running(r) /\ pc[r] = "postcommit"
    /\ MaybeDecline(r)
    /\ pc' = [pc EXCEPT ![r] = IF commitRes[r] = "yes" /\ eligible'[r] THEN "restore" ELSE "close"]
    /\ pass' = [pass EXCEPT ![r] = 1]
    /\ UNCHANGED <<commitRes, passesRun, restores, commits, closes, coll, waitTag, mismatch, aborted>>

\* Host mode, restore_entry_state: re-checks under the inventory lock, then rewinds.
Restore(r) ==
    /\ Running(r) /\ pc[r] = "restore"
    /\ MaybeDecline(r)
    /\ IF eligible'[r]
         THEN /\ restores' = [restores EXCEPT ![r] = @ + 1]
              /\ pc' = [pc EXCEPT ![r] = "body"]
         ELSE /\ pc' = [pc EXCEPT ![r] = "close"]
              /\ UNCHANGED restores
    /\ UNCHANGED <<commitRes, pass, passesRun, commits, closes, coll, waitTag, mismatch, aborted>>

\* Host mode: the re-executed pass re-runs the application's host code, collective included.
Body(r) ==
    /\ Running(r) /\ pc[r] = "body"
    /\ Enter(r, "IR", "endp")
    /\ UNCHANGED <<eligible, commitRes, pass, passesRun, restores, commits, closes, mismatch, aborted>>

\* Host mode, finish_pass for pass > 0: PASS exit, continue decision, then the re-checks.
EndPass(r) ==
    /\ Running(r) /\ pc[r] = "endp"
    /\ passesRun' = [passesRun EXCEPT ![r] = @ + 1]
    /\ MaybeDecline(r)
    /\ IF pass[r] + 1 < P /\ eligible'[r]
         THEN /\ pass' = [pass EXCEPT ![r] = @ + 1]
              /\ pc' = [pc EXCEPT ![r] = "restore"]
         ELSE /\ pc' = [pc EXCEPT ![r] = "close"]
              /\ UNCHANGED pass
    /\ UNCHANGED <<commitRes, restores, commits, closes, coll, waitTag, mismatch, aborted>>

\* Packet mode: after the commit nothing declines; the passes are GPU re-submissions only.
Gpu(r) ==
    /\ Running(r) /\ pc[r] = "gpu"
    /\ IF commitRes[r] = "yes" /\ eligible[r]
         THEN /\ passesRun' = [passesRun EXCEPT ![r] = P - 1]
              /\ restores' = [restores EXCEPT ![r] = P - 1]
         ELSE UNCHANGED <<passesRun, restores>>
    /\ pc' = [pc EXCEPT ![r] = "close"]
    /\ UNCHANGED <<eligible, commitRes, pass, commits, closes, coll, waitTag, mismatch, aborted>>

\* CLOSE. A tool that checks for a lost agreement aborts the job before issuing anything else.
Close(r) ==
    /\ Running(r) /\ pc[r] = "close"
    /\ closes' = [closes EXCEPT ![r] = @ + 1]
    /\ LET broken == commitRes[r] = "yes" /\ passesRun[r] < P - 1 IN
       IF AbortOnBroken /\ broken
         THEN /\ aborted' = TRUE
              /\ UNCHANGED <<pc, coll, waitTag>>
         ELSE /\ Enter(r, "POST", "done")
              /\ UNCHANGED aborted
    /\ UNCHANGED <<eligible, commitRes, pass, passesRun, restores, commits, mismatch>>

Done == (\A r \in Ranks : pc[r] = "done" /\ waitTag[r] = "none") \/ aborted

Next ==
    \/ CollectiveCompletes
    \/ \E r \in Ranks : Body0(r) \/ End0(r) \/ Commit(r) \/ PostCommit(r) \/ Restore(r)
                        \/ Body(r) \/ EndPass(r) \/ Gpu(r) \/ Close(r)
    \/ (Done /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars /\ WF_vars(Next)

-----------------------------------------------------------------------------
\* Safety: no collective ever completes with ranks in different calls.
NoMismatch == ~mismatch

\* The commit callback is delivered at most once, and before CLOSE.
CommitOnce == \A r \in Ranks : commits[r] <= 1 /\ (closes[r] > 0 => commits[r] = 1)

\* Nothing is rewound unless the commit said yes.
NoRestoreWithoutCommit == \A r \in Ranks : restores[r] > 0 => commitRes[r] = "yes"

\* CLOSE exactly once per range.
CloseOnce == \A r \in Ranks : closes[r] <= 1

\* When the job finishes normally, every rank ran the same number of passes.
Lockstep == (Done /\ ~aborted) => \A r, q \in Ranks : passesRun[r] = passesRun[q]

\* Liveness: the job always finishes (normally or by a diagnosed abort): no rank is stuck.
Terminates == <>Done
=============================================================================
