---- MODULE ReplayWindowLocksB5 ----
(***************************************************************************)
(* One agent, a range thread A and another thread B of the same process.   *)
(*                                                                         *)
(* Locks, as in the code:                                                  *)
(*  mq[q]  ROCr's InterceptQueue mutex. A doorbell store on an intercept   *)
(*         queue takes it and calls the SDK's WriteInterceptor while       *)
(*         holding it.                                                     *)
(*  L      hsa::agent_replay_mutex. Replay windows hold it as a writer;    *)
(*         WriteInterceptor takes it shared (queue.cpp) for every non-     *)
(*         replay dispatch.                                                *)
(*                                                                         *)
(* B owns one device buffer `cell`, written by its dispatches or async      *)
(* copies. Nothing in A's range touches it, so any restore that changes it  *)
(* loses B's write.                                                        *)
(*                                                                         *)
(* Fixed = FALSE is the code as it was: host replay fences every queue      *)
(* through its intercept doorbell while holding L; packet replay rings the  *)
(* range queue's intercept doorbell for each pass while holding L, and      *)
(* unregisters the range before taking L; copies take no lock.             *)
(* Fixed = TRUE: host replay fences before taking L; packet passes go to an *)
(* SDK-owned raw queue (no intercept mutex) and L is taken before the range *)
(* is unregistered; device-writing copies hold L shared across publishing   *)
(* the decline and submitting.                                             *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS Mode,        \* "packet" or "host"
          Fixed,       \* the protocol after the fix
          SharedQueue, \* B submits to the same HSA queue as A (HIP queue pooling)
          BOps,        \* subset of {"dispatch", "copy"}
          MaxB         \* number of operations B performs

QA == "QA"
QB == IF SharedQueue THEN "QA" ELSE "QB"
Queues == {"QA", "QB"}

VARIABLES pcA, pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap,
          exitSnap, bVal, bOp, bPending, lost, fenced

vars == <<pcA, pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
          bVal, bOp, bPending, lost, fenced>>

Init ==
    /\ pcA = "begin"
    /\ pcB = "idle"
    /\ mq = [q \in Queues |-> "free"]
    /\ lw = FALSE
    /\ lr = 0
    /\ open = FALSE
    /\ hostFlag = FALSE
    /\ declined = FALSE
    /\ cell = 0
    /\ landed = 0
    /\ entrySnap = 0
    /\ exitSnap = 0
    /\ bVal = 0
    /\ bOp = "none"
    /\ bPending = 0
    /\ lost = FALSE
    /\ fenced = {}

\* A restore writes back a saved value of B's buffer; if that is not B's last write, it is lost.
Restore(v) == /\ cell' = v
              /\ lost' = (lost \/ v # landed)

-----------------------------------------------------------------------------
(* Thread B. A dispatch: doorbell (mq), submission hook publishes a decline  *)
(* into an open range, then L shared across the write. A copy: publish then  *)
(* submit to the copy engine; with Fixed, L shared across both.              *)

BStart ==
    /\ pcB = "idle" /\ bVal < MaxB /\ bPending = 0
    /\ \E op \in BOps :
         /\ bOp' = op
         /\ pcB' = IF op = "dispatch" THEN "d_mq" ELSE IF Fixed THEN "c_rd" ELSE "c_pub"
    /\ bVal' = bVal + 1
    /\ UNCHANGED <<pcA, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bPending, lost, fenced>>

BDispatchMq ==
    /\ pcB = "d_mq" /\ mq[QB] = "free"
    /\ mq' = [mq EXCEPT ![QB] = "B"]
    /\ pcB' = "d_hook"
    /\ UNCHANGED <<pcA, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

\* note_foreign_dispatch: host ranges take any foreign dispatch; packet ranges only once bound.
BDispatchHook ==
    /\ pcB = "d_hook"
    /\ declined' = (declined \/ open)
    /\ pcB' = "d_rd"
    /\ UNCHANGED <<pcA, mq, lw, lr, open, hostFlag, cell, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, lost, fenced>>

BDispatchRd ==
    /\ pcB = "d_rd" /\ ~lw
    /\ lr' = 1
    /\ pcB' = IF Fixed THEN "d_rehook" ELSE "d_sub"
    /\ UNCHANGED <<pcA, mq, lw, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

\* With Fixed, WriteInterceptor publishes again once it holds the reader lock.
BDispatchRehook ==
    /\ pcB = "d_rehook"
    /\ declined' = (declined \/ open)
    /\ pcB' = "d_sub"
    /\ UNCHANGED <<pcA, mq, lw, lr, open, hostFlag, cell, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, lost, fenced>>

BDispatchSubmit ==
    /\ pcB = "d_sub"
    /\ bPending' = bVal
    /\ lr' = 0
    /\ mq' = [mq EXCEPT ![QB] = "free"]
    /\ pcB' = "idle"
    /\ UNCHANGED <<pcA, lw, open, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal,
                   bOp, lost, fenced>>

BCopyRd ==
    /\ pcB = "c_rd" /\ ~lw
    /\ lr' = 1
    /\ pcB' = "c_pub"
    /\ UNCHANGED <<pcA, mq, lw, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

\* note_device_write: declines every open range on the agent except a host range on B itself.
BCopyPublish ==
    /\ pcB = "c_pub"
    /\ declined' = (declined \/ open)
    /\ pcB' = "c_sub"
    /\ UNCHANGED <<pcA, mq, lw, lr, open, hostFlag, cell, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, lost, fenced>>

BCopySubmit ==
    /\ pcB = "c_sub"
    /\ bPending' = bVal
    /\ lr' = 0
    /\ pcB' = "idle"
    /\ UNCHANGED <<pcA, mq, lw, open, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal,
                   bOp, lost, fenced>>

\* The GPU or copy engine executes B's write at some later time.
BLands ==
    /\ bPending # 0
    /\ cell' = bPending
    /\ landed' = bPending
    /\ bPending' = 0
    /\ UNCHANGED <<pcA, pcB, mq, lw, lr, open, hostFlag, declined, entrySnap, exitSnap, bVal, bOp,
                   lost, fenced>>

-----------------------------------------------------------------------------
(* Thread A, packet mode. The range is open between begin and end; its entry *)
(* snapshot is taken at its first dispatch (inside its own interceptor, so  *)
(* the agent drain there sees every earlier dispatch complete).            *)

\* A drain: dispatches already submitted finish. Originally copies were invisible to every drain;
\* with Fixed, copies carry a ticket the drain waits for.
DrainDispatches == bPending = 0 \/ (bOp = "copy" /\ ~Fixed)

\* With Fixed the entry snapshot is taken under the writer lock: it waits for readers to leave.
PBegin ==
    /\ Mode = "packet" /\ pcA = "begin" /\ DrainDispatches
    /\ (Fixed => (lr = 0 /\ ~lw))
    /\ open' = TRUE
    /\ entrySnap' = cell
    /\ pcA' = IF Fixed THEN "p_lockw" ELSE "p_take"
    /\ UNCHANGED <<pcB, mq, lw, lr, hostFlag, declined, cell, landed, exitSnap, bVal, bOp,
                   bPending, lost, fenced>>

\* end(): take_range unregisters the range and folds the decline channel.
PTake ==
    /\ Mode = "packet" /\ pcA = "p_take"
    /\ open' = FALSE
    /\ pcA' = IF declined THEN "done" ELSE IF Fixed THEN "p_snap" ELSE "p_lockw"
    /\ lw' = (lw /\ ~declined)
    /\ UNCHANGED <<pcB, mq, lr, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal,
                   bOp, bPending, lost, fenced>>

PLockW ==
    /\ Mode = "packet" /\ pcA = "p_lockw" /\ lr = 0 /\ ~lw
    /\ lw' = TRUE
    /\ pcA' = IF Fixed THEN "p_take" ELSE "p_snap"
    /\ UNCHANGED <<pcB, mq, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal,
                   bOp, bPending, lost, fenced>>

\* Drain the agent, then take the exit snapshot.
PSnap ==
    /\ Mode = "packet" /\ pcA = "p_snap" /\ DrainDispatches
    /\ exitSnap' = cell
    /\ pcA' = "p_restore"
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, bVal, bOp,
                   bPending, lost, fenced>>

PRestore ==
    /\ Mode = "packet" /\ pcA = "p_restore"
    /\ Restore(entrySnap)
    /\ pcA' = IF Fixed THEN "p_pass" ELSE "p_mq"
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, landed, entrySnap, exitSnap, bVal,
                   bOp, bPending, fenced>>

\* The pass is submitted through the range queue's intercept doorbell.
PMq ==
    /\ Mode = "packet" /\ pcA = "p_mq" /\ mq[QA] = "free"
    /\ pcA' = "p_pass"
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

PPass ==
    /\ Mode = "packet" /\ pcA = "p_pass"
    /\ pcA' = "p_exit"
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

PExit ==
    /\ Mode = "packet" /\ pcA = "p_exit"
    /\ Restore(exitSnap)
    /\ lw' = FALSE
    /\ pcA' = "done"
    /\ UNCHANGED <<pcB, mq, lr, open, hostFlag, declined, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, fenced>>

-----------------------------------------------------------------------------
(* Thread A, host mode: begin() snapshots; end() of pass 0 re-checks and     *)
(* restores; end() of pass 1 closes. Each end() fences every queue.        *)

HBegin ==
    /\ Mode = "host" /\ pcA = "begin"
    /\ open' = TRUE
    /\ hostFlag' = TRUE
    /\ fenced' = {}
    /\ pcA' = IF Fixed THEN "h_fence0" ELSE "h_lock0"
    /\ UNCHANGED <<pcB, mq, lw, lr, declined, cell, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, lost>>

\* Lock acquisition: lock_agents.
HLock(from, to) ==
    /\ Mode = "host" /\ pcA = from /\ lr = 0 /\ ~lw
    /\ lw' = TRUE
    /\ pcA' = to
    /\ UNCHANGED <<pcB, mq, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal,
                   bOp, bPending, lost, fenced>>

\* fence_queue: a barrier through the queue's intercept doorbell, then wait for it; the barrier
\* completes after every dispatch submitted to that queue before it. Originally the agent drain
\* followed the barriers; with Fixed it runs separately, under the locks (HDrain).
HFence(from, to) ==
    /\ Mode = "host" /\ pcA = from
    /\ IF fenced = Queues
         THEN /\ Fixed \/ DrainDispatches
              /\ pcA' = to
              /\ fenced' = {}
         ELSE /\ \E q \in Queues \ fenced :
                   /\ mq[q] = "free"
                   /\ (q # QB \/ bOp # "dispatch" \/ bPending = 0)
                   /\ fenced' = fenced \cup {q}
              /\ UNCHANGED pcA
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost>>

HDrain(from, to) ==
    /\ Mode = "host" /\ pcA = from /\ DrainDispatches
    /\ pcA' = to
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

HSnap ==
    /\ Mode = "host" /\ pcA = "h_snap0"
    /\ entrySnap' = cell
    /\ declined' = declined
    /\ lw' = FALSE
    /\ pcA' = "h_body0"
    /\ UNCHANGED <<pcB, mq, lr, open, hostFlag, cell, landed, exitSnap, bVal, bOp, bPending, lost,
                   fenced>>

\* Pass 0 / pass 1 of the application's code: its own dispatch on QA through the doorbell.
HBody(from, to) ==
    /\ Mode = "host" /\ pcA = from /\ mq[QA] = "free" /\ ~lw
    /\ pcA' = to
    /\ UNCHANGED <<pcB, mq, lw, lr, open, hostFlag, declined, cell, landed, entrySnap, exitSnap,
                   bVal, bOp, bPending, lost, fenced>>

\* finish_pass folds the decline channel (and runs the commit); restore_entry_state then
\* copies the snapshot back. Both happen with L held.
HFold ==
    /\ Mode = "host" /\ pcA = "h_restore"
    /\ IF declined
         THEN /\ lw' = FALSE
              /\ open' = FALSE
              /\ pcA' = "done"
         ELSE /\ pcA' = "h_copyback"
              /\ UNCHANGED <<lw, open>>
    /\ UNCHANGED <<pcB, mq, lr, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, lost, fenced>>

HRestore ==
    /\ Mode = "host" /\ pcA = "h_copyback"
    /\ Restore(entrySnap)
    /\ lw' = FALSE
    /\ pcA' = "h_body1"
    /\ UNCHANGED <<pcB, mq, lr, open, hostFlag, declined, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, fenced>>

HClose ==
    /\ Mode = "host" /\ pcA = "h_close"
    /\ lw' = FALSE
    /\ open' = FALSE
    /\ pcA' = "done"
    /\ UNCHANGED <<pcB, mq, lr, hostFlag, declined, cell, landed, entrySnap, exitSnap, bVal, bOp,
                   bPending, lost, fenced>>

HostSteps ==
    \/ HBegin
    \/ HLock("h_lock0", "h_fence0b") \/ HFence("h_fence0b", "h_snap0")
    \/ HFence("h_fence0", "h_lock0f") \/ HLock("h_lock0f", "h_drain0") \/ HDrain("h_drain0", "h_snap0")
    \/ HSnap
    \/ HBody("h_body0", IF Fixed THEN "h_fence1" ELSE "h_lock1")
    \/ HLock("h_lock1", "h_fence1b") \/ HFence("h_fence1b", "h_restore")
    \/ HFence("h_fence1", "h_lock1f") \/ HLock("h_lock1f", "h_drain1") \/ HDrain("h_drain1", "h_restore")
    \/ HFold \/ HRestore
    \/ HBody("h_body1", IF Fixed THEN "h_fence2" ELSE "h_lock2")
    \/ HLock("h_lock2", "h_fence2b") \/ HFence("h_fence2b", "h_close")
    \/ HFence("h_fence2", "h_lock2f") \/ HLock("h_lock2f", "h_drain2") \/ HDrain("h_drain2", "h_close")
    \/ HClose

PacketSteps == PBegin \/ PTake \/ PLockW \/ PSnap \/ PRestore \/ PMq \/ PPass \/ PExit

Terminated == pcA = "done" /\ pcB = "idle" /\ (bVal = MaxB) /\ bPending = 0

Next ==
    \/ BStart \/ BDispatchMq \/ BDispatchHook \/ BDispatchRd \/ BDispatchRehook \/ BDispatchSubmit
    \/ BCopyRd \/ BCopyPublish \/ BCopySubmit \/ BLands
    \/ PacketSteps \/ HostSteps
    \/ (Terminated /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

\* B's writes are never reverted by a restore of A's range.
NoLostWrite == ~lost
=============================================================================
