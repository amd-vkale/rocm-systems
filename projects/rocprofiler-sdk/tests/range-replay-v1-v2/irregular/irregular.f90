! Irregular-access (and one regular) OpenMP offload kernels in Fortran, in a replayable range.
!   irregular-f <spmv|gups|hist|gather|chase|jacobi>
! Inputs stay mapped on the device; range 1 holds ITERS kernels (RR_RANGE=full adds the input
! upload and result download). Each result is checked against a serial host reference.
module rr_bridge
  use iso_c_binding
  implicit none
  interface
    subroutine rr_f_range(id, body) bind(c, name='rr_f_range')
      import :: c_int64_t, c_funptr
      integer(c_int64_t), value :: id
      type(c_funptr), value :: body
    end subroutine
    subroutine rr_f_track(a, bytes) bind(c, name='rr_f_track')
      import :: c_int64_t
      type(*), dimension(*) :: a
      integer(c_int64_t), value :: bytes
    end subroutine
    integer(c_int) function rr_f_range_is_full() bind(c, name='rr_f_range_is_full')
      import :: c_int
    end function
    type(c_ptr) function rr_f_mode_name() bind(c, name='rr_f_mode_name')
      import :: c_ptr
    end function
  end interface
contains
  function mode_name() result(s)
    character(len=16) :: s
    character(kind=c_char), pointer :: p(:)
    integer :: i
    call c_f_pointer(rr_f_mode_name(), p, [16])
    s = ' '
    do i = 1, 16
      if (p(i) == c_null_char) exit
      s(i:i) = p(i)
    end do
  end function
end module

module bench_state
  use iso_c_binding
  implicit none
  integer, parameter :: iters = 8
  integer(8), parameter :: k1 = int(z'ff51afd7ed558ccd', 8), k2 = int(z'c4ceb9fe1a85ec53', 8)
  logical :: full = .false.
  real(8) :: tk0 = 0, tk1 = 0
  ! spmv
  integer(8) :: rows, kk, nnz
  integer(4), allocatable :: col(:)
  real(8), allocatable :: val(:), x(:), y(:)
  ! gups
  integer(8) :: tn, nupd
  integer(8), allocatable :: tab(:)
  ! hist
  integer(8) :: hn
  integer(8), parameter :: bins = 4096
  integer(4), allocatable :: key(:)
  integer(8), allocatable :: h(:)
  ! gather
  integer(8) :: gn
  integer(8), allocatable :: idx(:), perm(:)
  real(8), allocatable :: gx(:), gy(:)
  ! chase
  integer(8) :: cn
  integer(8), parameter :: walkers = 65536, steps = 256
  integer(4), allocatable :: nxt(:), pos(:)
  ! jacobi
  integer, parameter :: jn = 4096
  real(8), allocatable :: ja(:,:), jb(:,:)
contains
  pure integer(8) function mix64(v)
    !$omp declare target
    integer(8), value :: v
    integer(8) :: z
    z = v
    z = ieor(z, shiftr(z, 33))
    z = z * k1
    z = ieor(z, shiftr(z, 33))
    z = z * k2
    z = ieor(z, shiftr(z, 33))
    mix64 = z
  end function

  ! Unsigned modulo of a 64-bit hash.
  pure integer(8) function umod(v, m)
    !$omp declare target
    integer(8), value :: v, m
    umod = modulo(shiftr(v, 1), m)
  end function

  real(8) function wtime_ms()
    integer(8) :: c, r
    call system_clock(c, r)
    wtime_ms = real(c, 8) * 1000d0 / real(r, 8)
  end function

  subroutine spmv_body() bind(c)
    integer :: it
    integer(8) :: r, j
    real(8) :: s
    if (full) then
      !$omp target update to(x)
    end if
    tk0 = wtime_ms()
    do it = 1, iters
      !$omp target teams distribute parallel do private(s, j)
      do r = 1, rows
        s = 0
        do j = (r - 1) * kk + 1, r * kk
          s = s + val(j) * x(col(j))
        end do
        y(r) = s
      end do
      !$omp target teams distribute parallel do
      do r = 1, rows
        x(r) = 0.5d0 * y(r) + 0.25d0
      end do
    end do
    tk1 = wtime_ms()
    if (full) then
      !$omp target update from(x)
    end if
  end subroutine

  subroutine gups_body() bind(c)
    integer :: it
    integer(8) :: u, v, slot
    if (full) then
      !$omp target update to(tab)
    end if
    tk0 = wtime_ms()
    do it = 1, iters
      !$omp target teams distribute parallel do private(v, slot)
      do u = 0, nupd - 1
        v = mix64(u * iters + it)
        slot = umod(v, tn) + 1
        !$omp atomic update
        tab(slot) = ieor(tab(slot), v)
      end do
    end do
    tk1 = wtime_ms()
    if (full) then
      !$omp target update from(tab)
    end if
  end subroutine

  subroutine hist_body() bind(c)
    integer :: it
    integer(8) :: i, b
    if (full) then
      !$omp target update to(h)
    end if
    tk0 = wtime_ms()
    do it = 1, iters
      !$omp target teams distribute parallel do private(b)
      do i = 1, hn
        b = modulo(int(key(i), 8) + it, bins) + 1
        !$omp atomic update
        h(b) = h(b) + 1
      end do
    end do
    tk1 = wtime_ms()
    if (full) then
      !$omp target update from(h)
    end if
  end subroutine

  subroutine gather_body() bind(c)
    integer :: it
    integer(8) :: i
    if (full) then
      !$omp target update to(gy)
    end if
    tk0 = wtime_ms()
    do it = 1, iters
      !$omp target teams distribute parallel do
      do i = 1, gn
        gy(perm(i)) = 0.5d0 * gy(perm(i)) + gx(idx(i))
      end do
    end do
    tk1 = wtime_ms()
    if (full) then
      !$omp target update from(gy)
    end if
  end subroutine

  subroutine chase_body() bind(c)
    integer :: it
    integer(8) :: w, s
    integer(4) :: p
    if (full) then
      !$omp target update to(pos)
    end if
    tk0 = wtime_ms()
    do it = 1, iters
      !$omp target teams distribute parallel do private(p, s)
      do w = 1, walkers
        p = pos(w)
        do s = 1, steps
          p = nxt(p)
        end do
        pos(w) = p
      end do
    end do
    tk1 = wtime_ms()
    if (full) then
      !$omp target update from(pos)
    end if
  end subroutine

  subroutine jacobi_body() bind(c)
    integer :: it, i, j
    if (full) then
      !$omp target update to(ja)
    end if
    tk0 = wtime_ms()
    do it = 1, iters
      !$omp target teams distribute parallel do collapse(2)
      do j = 2, jn - 1
        do i = 2, jn - 1
          jb(i, j) = 0.25d0 * (ja(i - 1, j) + ja(i + 1, j) + ja(i, j - 1) + ja(i, j + 1))
        end do
      end do
      !$omp target teams distribute parallel do collapse(2)
      do j = 2, jn - 1
        do i = 2, jn - 1
          ja(i, j) = jb(i, j)
        end do
      end do
    end do
    tk1 = wtime_ms()
    if (full) then
      !$omp target update from(ja)
    end if
  end subroutine
end module

program irregular_f
  use iso_c_binding
  use rr_bridge
  use bench_state
  implicit none
  character(len=16) :: name
  real(8) :: t0, t1, err
  integer(8) :: i, j, w, s, bad, v, slot, tmp
  integer :: it, ii, jj
  integer(4) :: p, t4
  real(8) :: acc
  integer(8), allocatable :: rtab(:), rh(:)
  real(8), allocatable :: rx(:), ry(:), rgy(:), ra(:,:), rb(:,:)
  integer(4), allocatable :: rpos(:)

  call get_command_argument(1, name)
  full = rr_f_range_is_full() /= 0
  !$omp target
  !$omp end target

  select case (trim(name))
  case ('spmv')
    rows = 2_8**20; kk = 16; nnz = rows * kk
    allocate(col(nnz), val(nnz), x(rows), y(rows), rx(rows), ry(rows))
    do i = 1, nnz
      col(i) = int(umod(mix64(i), rows) + 1, 4)
      val(i) = 1d0 / real(kk, 8) * (0.5d0 + real(umod(mix64(i + nnz), 1000_8), 8) / 1000d0)
    end do
    do i = 1, rows
      x(i) = 1d0 + real(mod(i - 1, 7_8), 8)
    end do
    rx = x
    !$omp target enter data map(to: col, val, x) map(alloc: y)
    t0 = wtime_ms()
    if (full) call rr_f_track(x, int(storage_size(x)/8, c_int64_t) * size(x, kind=c_int64_t))
    call rr_f_range(1_c_int64_t, c_funloc(spmv_body))
    t1 = wtime_ms()
    if (.not. full) then
      !$omp target update from(x)
    end if
    do it = 1, iters
      do i = 1, rows
        acc = 0
        do j = (i - 1) * kk + 1, i * kk
          acc = acc + val(j) * rx(col(j))
        end do
        ry(i) = acc
      end do
      rx = 0.5d0 * ry + 0.25d0
    end do
    err = maxval(abs(x - rx) / max(1d-30, abs(rx)))
    call report(err < 1d-12)
  case ('gups')
    tn = 2_8**24; nupd = 4 * tn
    allocate(tab(tn), rtab(tn))
    do i = 1, tn
      tab(i) = i - 1
    end do
    rtab = tab
    !$omp target enter data map(to: tab)
    t0 = wtime_ms()
    if (full) call rr_f_track(tab, int(storage_size(tab)/8, c_int64_t) * size(tab, kind=c_int64_t))
    call rr_f_range(1_c_int64_t, c_funloc(gups_body))
    t1 = wtime_ms()
    if (.not. full) then
      !$omp target update from(tab)
    end if
    do it = 1, iters
      do i = 0, nupd - 1
        v = mix64(i * iters + it)
        slot = umod(v, tn) + 1
        rtab(slot) = ieor(rtab(slot), v)
      end do
    end do
    err = real(count(tab /= rtab), 8)
    call report(err == 0)
  case ('hist')
    hn = 2_8**26
    allocate(key(hn), h(bins), rh(bins))
    do i = 1, hn
      v = mix64(i)
      if (iand(v, 1_8) == 1) then
        key(i) = int(umod(shiftr(v, 8), 64_8), 4)
      else
        key(i) = int(umod(shiftr(v, 8), bins), 4)
      end if
    end do
    h = 0; rh = 0
    !$omp target enter data map(to: key, h)
    t0 = wtime_ms()
    if (full) call rr_f_track(h, int(storage_size(h)/8, c_int64_t) * size(h, kind=c_int64_t))
    call rr_f_range(1_c_int64_t, c_funloc(hist_body))
    t1 = wtime_ms()
    if (.not. full) then
      !$omp target update from(h)
    end if
    do it = 1, iters
      do i = 1, hn
        j = modulo(int(key(i), 8) + it, bins) + 1
        rh(j) = rh(j) + 1
      end do
    end do
    err = real(count(h /= rh), 8)
    call report(err == 0)
  case ('gather')
    gn = 2_8**24
    allocate(idx(gn), perm(gn), gx(gn), gy(gn), rgy(gn))
    do i = 1, gn
      idx(i) = umod(mix64(i), gn) + 1
      perm(i) = i
      gx(i) = real(mod(i - 1, 101_8), 8) * 0.01d0
    end do
    do i = gn, 2, -1
      j = umod(mix64(i + 7 * gn), i) + 1
      tmp = perm(i); perm(i) = perm(j); perm(j) = tmp
    end do
    gy = 1d0; rgy = gy
    !$omp target enter data map(to: idx, perm, gx, gy)
    t0 = wtime_ms()
    if (full) call rr_f_track(gy, int(storage_size(gy)/8, c_int64_t) * size(gy, kind=c_int64_t))
    call rr_f_range(1_c_int64_t, c_funloc(gather_body))
    t1 = wtime_ms()
    if (.not. full) then
      !$omp target update from(gy)
    end if
    do it = 1, iters
      do i = 1, gn
        rgy(perm(i)) = 0.5d0 * rgy(perm(i)) + gx(idx(i))
      end do
    end do
    err = maxval(abs(gy - rgy))
    call report(err < 1d-12)
  case ('chase')
    cn = 2_8**24
    allocate(nxt(cn), pos(walkers), rpos(walkers))
    do i = 1, cn
      nxt(i) = int(i, 4)
    end do
    do i = cn, 2, -1
      j = umod(mix64(i), i - 1) + 1
      t4 = nxt(i); nxt(i) = nxt(j); nxt(j) = t4
    end do
    do w = 1, walkers
      pos(w) = int((w - 1) * (cn / walkers) + 1, 4)
    end do
    rpos = pos
    !$omp target enter data map(to: nxt, pos)
    t0 = wtime_ms()
    if (full) call rr_f_track(pos, int(storage_size(pos)/8, c_int64_t) * size(pos, kind=c_int64_t))
    call rr_f_range(1_c_int64_t, c_funloc(chase_body))
    t1 = wtime_ms()
    if (.not. full) then
      !$omp target update from(pos)
    end if
    do it = 1, iters
      do w = 1, walkers
        p = rpos(w)
        do s = 1, steps
          p = nxt(p)
        end do
        rpos(w) = p
      end do
    end do
    err = real(count(pos /= rpos), 8)
    call report(err == 0)
  case ('jacobi')
    allocate(ja(jn, jn), jb(jn, jn), ra(jn, jn), rb(jn, jn))
    do jj = 1, jn
      do ii = 1, jn
        ja(ii, jj) = real(mod(ii * 7 + jj * 13, 97), 8) * 0.01d0
      end do
    end do
    jb = ja; ra = ja; rb = ja
    !$omp target enter data map(to: ja, jb)
    t0 = wtime_ms()
    if (full) call rr_f_track(ja, int(storage_size(ja)/8, c_int64_t) * size(ja, kind=c_int64_t))
    call rr_f_range(1_c_int64_t, c_funloc(jacobi_body))
    t1 = wtime_ms()
    if (.not. full) then
      !$omp target update from(ja)
    end if
    do it = 1, iters
      do jj = 2, jn - 1
        do ii = 2, jn - 1
          rb(ii, jj) = 0.25d0 * (ra(ii - 1, jj) + ra(ii + 1, jj) + ra(ii, jj - 1) + ra(ii, jj + 1))
        end do
      end do
      ra(2:jn-1, 2:jn-1) = rb(2:jn-1, 2:jn-1)
    end do
    err = maxval(abs(ja - ra))
    call report(err < 1d-12)
  case default
    print *, 'usage: irregular-f spmv|gups|hist|gather|chase|jacobi'
    stop 2
  end select

contains
  subroutine report(ok)
    logical, intent(in) :: ok
    character(len=8) :: rng
    rng = 'kernel'
    if (full) rng = 'full'
    write(*, '(a,a,a,a,a,a,a,f0.3,a,f0.3,a,es10.3,a,i1)') '[bench] name=', trim(name), &
      ' lang=fortran range=', trim(rng), ' mode=', trim(mode_name()), ' range_ms=', t1 - t0, &
      ' kernels_ms=', tk1 - tk0, ' err=', err, ' valid=', merge(1, 0, ok)
    if (.not. ok) stop 1
  end subroutine
end program
