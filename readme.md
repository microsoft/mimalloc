
<img align="left" width="100" height="100" src="doc/mimalloc-logo.png"/>

# mimalloc

&nbsp;

mimalloc (pronounced "me-malloc")
is a general purpose allocator with excellent [performance](#performance) characteristics.
Initially developed by Daan Leijen for the run-time systems of the
[Koka](https://github.com/koka-lang/koka) and [Lean](https://github.com/leanprover/lean) languages.

It is a drop-in replacement for `malloc` and can be used in other programs
without code changes, for example, on Unix you can use it as:
```
> LD_PRELOAD=/usr/bin/libmimalloc.so  myprogram
```

Notable aspects of the design include:

- __small and consistent__: the library is less than 3500 LOC using simple and
  consistent data structures. This makes it very suitable
  to integrate and adapt in other projects. For runtime systems it
  provides hooks for a monotonic _heartbeat_ and deferred freeing (for
  bounded worst-case times with reference counting).
- __free list sharding__: the big idea: instead of one big free list (per size class) we have
  many smaller lists per memory "page" which both reduces fragmentation
  and increases locality --
  things that are allocated close in time get allocated close in memory.
  (A memory "page" in _mimalloc_ contains blocks of one size class and is
  usually 64KiB on a 64-bit system).
- __eager page reset__: when a "page" becomes empty (with increased chance
  due to free list sharding) the memory is marked to the OS as unused ("reset" or "purged")
  reducing (real) memory pressure and fragmentation, especially in long running
  programs.
- __secure__: _mimalloc_ can be build in secure mode, adding guard pages,
  randomized allocation, encrypted free lists, etc. to protect against various
  heap vulnerabilities. The performance penalty is only around 3% on average
  over our benchmarks.
- __first-class heaps__: efficiently create and use multiple heaps to allocate across different regions.
  A heap can be destroyed at once instead of deallocating each object separately.  
- __bounded__: it does not suffer from _blowup_ \[1\], has bounded worst-case allocation
  times (_wcat_), bounded space overhead (~0.2% meta-data, with at most 16.7% waste in allocation sizes),
  and has no internal points of contention using only atomic operations.
- __fast__: In our benchmarks (see [below](#performance)),
  _mimalloc_ always outperforms all other leading allocators (_jemalloc_, _tcmalloc_, _Hoard_, etc),
  and usually uses less memory (up to 25% more in the worst case). A nice property
  is that it does consistently well over a wide range of benchmarks.

You can read more on the design of _mimalloc_ in the upcoming technical report
which also has detailed benchmark results.   

Enjoy!  

# Building

## Windows

Open `ide/vs2017/mimalloc.sln` in Visual Studio 2017 and build.
The `mimalloc` project builds a static library (in `out/msvc-x64`), while the
`mimalloc-override` project builds a DLL for overriding malloc
in the entire program.

## MacOSX, Linux, BSD, etc.

We use [`cmake`](https://cmake.org)<sup>1</sup> as the build system:

```
> mkdir -p out/release
> cd out/release
> cmake ../..
> make
```
This builds the library as a shared (dynamic)
library (`.so` or `.dylib`), a static library (`.a`), and
as a single object file (`.o`).

`> sudo make install` (install the library and header files in `/usr/local/lib`  and `/usr/local/include`)

You can build the debug version which does many internal checks and
maintains detailed statistics as:

```
> mkdir -p out/debug
> cd out/debug
> cmake -DCMAKE_BUILD_TYPE=Debug ../..
> make
```
This will name the shared library as `libmimalloc-debug.so`.

Finally, you can build a _secure_ version that uses guard pages, encrypted
free lists, etc, as:
```
> mkdir -p out/secure
> cd out/secure
> cmake -DSECURE=ON ../..
> make
```
This will name the shared library as `libmimalloc-secure.so`.
Use `ccmake`<sup>2</sup> instead of `cmake`
to see and customize all the available build options.

Notes:
1. Install CMake: `sudo apt-get install cmake`
2. Install CCMake: `sudo apt-get install cmake-curses-gui`



# Using the library

The preferred usage is including `<mimalloc.h>`, linking with
the shared- or static library, and using the `mi_malloc` API exclusively for allocation. For example,
```
gcc -o myprogram -lmimalloc myfile.c
```

mimalloc uses only safe OS calls (`mmap` and `VirtualAlloc`) and can co-exist
with other allocators linked to the same program.
If you use `cmake`, you can simply use:
```
find_package(mimalloc 1.0 REQUIRED)
```
in your `CMakeLists.txt` to find a locally installed mimalloc. Then use either:
```
target_link_libraries(myapp PUBLIC mimalloc)
```
to link with the shared (dynamic) library, or:
```
target_link_libraries(myapp PUBLIC mimalloc-static)
```
to link with the static library. See `test\CMakeLists.txt` for an example.


You can pass environment variables to print verbose messages (`MIMALLOC_VERBOSE=1`)
and statistics (`MIMALLOC_STATS=1`) (in the debug version):
```
> env MIMALLOC_STATS=1 ./cfrac 175451865205073170563711388363

175451865205073170563711388363 = 374456281610909315237213 * 468551

heap stats:     peak      total      freed       unit
normal   2:    16.4 kb    17.5 mb    17.5 mb      16 b   ok
normal   3:    16.3 kb    15.2 mb    15.2 mb      24 b   ok
normal   4:      64 b      4.6 kb     4.6 kb      32 b   ok
normal   5:      80 b    118.4 kb   118.4 kb      40 b   ok
normal   6:      48 b       48 b       48 b       48 b   ok
normal  17:     960 b      960 b      960 b      320 b   ok

heap stats:     peak      total      freed       unit
    normal:    33.9 kb    32.8 mb    32.8 mb       1 b   ok
      huge:       0 b        0 b        0 b        1 b   ok
     total:    33.9 kb    32.8 mb    32.8 mb       1 b   ok
malloc requested:         32.8 mb

 committed:    58.2 kb    58.2 kb    58.2 kb       1 b   ok
  reserved:     2.0 mb     2.0 mb     2.0 mb       1 b   ok
     reset:       0 b        0 b        0 b        1 b   ok
  segments:       1          1          1
-abandoned:       0
     pages:       6          6          6
-abandoned:       0
     mmaps:       3
 mmap fast:       0
 mmap slow:       1
   threads:       0
   elapsed:     2.022s
   process: user: 1.781s, system: 0.016s, faults: 756, reclaims: 0, rss: 2.7 mb
```

The above model of using the `mi_` prefixed API is not always possible
though in existing programs that already use the standard malloc interface,
and another option is to override the standard malloc interface
completely and redirect all calls to the _mimalloc_ library instead.



# Overriding Malloc

Overriding the standard `malloc` can be done either _dynamically_ or _statically_.

## Dynamic override

This is the recommended way to override the standard malloc interface.

### Unix, BSD, MacOSX

On these systems we preload the mimalloc shared
library so all calls to the standard `malloc` interface are
resolved to the _mimalloc_ library.

- `env LD_PRELOAD=/usr/lib/libmimalloc.so myprogram` (on Linux, BSD, etc.)
- `env DYLD_INSERT_LIBRARIES=usr/lib/libmimalloc.dylib myprogram` (On MacOSX)

  Note certain security restrictions may apply when doing this from
  the [shell](https://stackoverflow.com/questions/43941322/dyld-insert-libraries-ignored-when-calling-application-through-bash).

You can set extra environment variables to check that mimalloc is running,
like:
```
env MIMALLOC_VERBOSE=1 LD_PRELOAD=/usr/lib/libmimalloc.so myprogram
```
or run with the debug version to get detailed statistics:
```
env MIMALLOC_STATS=1 LD_PRELOAD=/usr/lib/libmimalloc-debug.so myprogram
```

### Windows

On Windows you need to link your program explicitly with the mimalloc
DLL, and use the C-runtime library as a DLL (the `/MD` or `/MDd` switch).
To ensure the mimalloc DLL gets loaded it is easiest to insert some
call to the mimalloc API in the `main` function, like `mi_version()`.

Due to the way mimalloc intercepts the standard malloc at runtime, it is best
to link to the mimalloc import library first on the command line so it gets
loaded right after the universal C runtime DLL (`ucrtbase`). See
the `mimalloc-override-test` project for an example.


## Static override

On Unix systems, you can also statically link with _mimalloc_ to override the standard
malloc interface. The recommended way is to link the final program with the
_mimalloc_ single object file (`mimalloc-override.o`). We use
an object file instead of a library file as linkers give preference to
that over archives to resolve symbols. To ensure that the standard
malloc interface resolves to the _mimalloc_ library, link it as the first
object file. For example:

```
gcc -o myprogram mimalloc-override.o  myfile1.c ...
```


# Performance

We tested _mimalloc_ against many other top allocators over a wide
range of benchmarks, ranging from various real world programs to
synthetic benchmarks that see how the allocator behaves under more
extreme circumstances.

In our benchmarks, _mimalloc_ always outperforms all other leading
allocators (_jemalloc_, _tcmalloc_, _Hoard_, etc), and usually uses less
memory (up to 25% more in the worst case). A nice property is that it
does *consistently* well over the wide range of benchmarks.

Allocators are interesting as there exists no algorithm that is generally
optimal -- for a given allocator one can usually construct a workload
where it does not do so well. The goal is thus to find an allocation
strategy that performs well over a wide range of benchmarks without
suffering from underperformance in less common situations (which is what
the second half of our benchmark set tests for).

We show here only the results on an AMD EPYC system (Apr 2019) -- for
specific details and further benchmarks we refer to the technical report.

The benchmark suite is scripted and available separately
as [mimalloc-bench](https://github.com/daanx/mimalloc-bench).


## Benchmark Results

Testing on a big Amazon EC2 instance ([r5a.4xlarge](https://aws.amazon.com/ec2/instance-types/))
consisting of a 16-core AMD EPYC 7000 at 2.5GHz
with 128GB ECC memory, running	Ubuntu 18.04.1 with LibC 2.27 and GCC 7.3.0.
The measured allocators are _mimalloc_ (mi),
Google's [_tcmalloc_](https://github.com/gperftools/gperftools) (tc) used in Chrome,
[_jemalloc_](https://github.com/jemalloc/jemalloc) (je) by Jason Evans used in Firefox and FreeBSD,
[_snmalloc_](https://github.com/microsoft/snmalloc) (sn) by Liétar et al. \[8], [_rpmalloc_](https://github.com/rampantpixels/rpmalloc) (rp) by Mattias Jansson at Rampant Pixels,
[_Hoard_](https://github.com/emeryberger/Hoard) by Emery Berger \[1],
the system allocator (glibc) (based on _PtMalloc2_), and the Intel thread
building blocks [allocator](https://github.com/intel/tbb) (tbb).

![bench-r5a-1](doc/bench-r5a-1.svg)
![bench-r5a-2](doc/bench-r5a-2.svg)

Memory usage:

![bench-r5a-rss-1](doc/bench-r5a-rss-1.svg)
![bench-r5a-rss-1](doc/bench-r5a-rss-2.svg)

(note: the _xmalloc-testN_ memory usage should be disregarded is it
allocates more the faster the program runs).

In the first five benchmarks we can see _mimalloc_ outperforms the other
allocators moderately, but we also see that all these modern allocators
perform well -- the times of large performance differences in regular
workloads are over :-).
In _cfrac_ and _espresso_, _mimalloc_ is a tad faster than _tcmalloc_ and
_jemalloc_, but a solid 10\% faster than all other allocators on
_espresso_. The _tbb_ allocator does not do so well here and lags more than
20\% behind _mimalloc_. The _cfrac_ and _espresso_ programs do not use much
memory (~1.5MB) so it does not matter too much, but still _mimalloc_ uses
about half the resident memory of _tcmalloc_.

The _leanN_ program is most interesting as a large realistic and
concurrent workload of the [Lean](https://github.com/leanprover/lean) theorem prover
compiling its own standard library, and there is a 8% speedup over _tcmalloc_. This is
quite significant: if Lean spends 20% of its time in the
allocator that means that _mimalloc_ is 1.3&times; faster than _tcmalloc_
here. (This is surprising as that is not measured in a pure
allocation benchmark like _alloc-test_. We conjecture that we see this
outsized improvement here because _mimalloc_ has better locality in
the allocation which improves performance for the *other* computations
in a program as well).

The _redis_ benchmark shows more differences between the allocators where
_mimalloc_ is 14\% faster than _jemalloc_. On this benchmark _tbb_ (and _Hoard_) do
not do well and are over 40\% slower.

The _larson_ server workload allocates and frees objects between
many threads. Larson and Krishnan \[2] observe this
behavior (which they call _bleeding_) in actual server applications, and the
benchmark simulates this.
Here, _mimalloc_ is more than 2.5&times; faster than _tcmalloc_ and _jemalloc_
due to the object migration between different threads. This is a difficult
benchmark for other allocators too where _mimalloc_ is still 48% faster than the next
fastest (_snmalloc_).


The second benchmark set tests specific aspects of the allocators and
shows even more extreme differences between them.

The _alloc-test_, by
[OLogN Technologies AG](http://ithare.com/testing-memory-allocators-ptmalloc2-tcmalloc-hoard-jemalloc-while-trying-to-simulate-real-world-loads/), is a very allocation intensive benchmark doing millions of
allocations in various size classes. The test is scaled such that when an
allocator performs almost identically on _alloc-test1_ as _alloc-testN_ it
means that it scales linearly. Here, _tcmalloc_, _snmalloc_, and
_Hoard_ seem to scale less well and do more than 10% worse on the
multi-core version. Even the best allocators (_tcmalloc_ and _jemalloc_) are
more than 10% slower as _mimalloc_ here.

The _sh6bench_ and _sh8bench_ benchmarks are
developed by [MicroQuill](http://www.microquill.com/) as part of SmartHeap.
In _sh6bench_ _mimalloc_ does much
better than the others (more than 2&times; faster than _jemalloc_).
We cannot explain this well but believe it is
caused in part by the "reverse" free-ing pattern in _sh6bench_.
Again in _sh8bench_ the _mimalloc_ allocator handles object migration
between threads much better and is over 36% faster than the next best
allocator, _snmalloc_. Whereas _tcmalloc_ did well on _sh6bench_, the
addition of object migration caused it to be almost 3 times slower
than before.

The _xmalloc-testN_ benchmark by Lever and Boreham \[5] and Christian Eder,
simulates an asymmetric workload where
some threads only allocate, and others only free. The _snmalloc_
allocator was especially developed to handle this case well as it
often occurs in concurrent message passing systems (like the [Pony] language
for which _snmalloc_ was initially developed). Here we see that
the _mimalloc_ technique of having non-contended sharded thread free
lists pays off as it even outperforms _snmalloc_ here.
Only _jemalloc_ also handles this reasonably well, while the
others underperform by a large margin.

The _cache-scratch_ benchmark by Emery Berger \[1], and introduced with the Hoard
allocator to test for _passive-false_ sharing of cache lines. With a single thread they all
perform the same, but when running with multiple threads the potential allocator
induced false sharing of the cache lines causes large run-time
differences, where _mimalloc_ is more than 18&times; faster than _jemalloc_ and
_tcmalloc_! Crundal \[6] describes in detail why the false cache line
sharing occurs in the _tcmalloc_ design, and also discusses how this
can be avoided with some small implementation changes.
Only _snmalloc_ and _tbb_ also avoid the
cache line sharing like _mimalloc_. Kukanov and Voss \[7] describe in detail
how the design of _tbb_ avoids the false cache line sharing.



<!--

## Tested Allocators

We tested _mimalloc_ with 9 leading allocators over 12 benchmarks
and the SpecMark benchmarks. The tested allocators are:

- mi: The _mimalloc_ allocator, using version tag `v1.0.0`.
  We also test a secure version of _mimalloc_ as smi which uses
  the techniques described in Section [#sec-secure].
- tc: The [_tcmalloc_](https://github.com/gperftools/gperftools)
  allocator which comes as part of
  the Google performance tools and is used in the Chrome browser.
  Installed as package `libgoogle-perftools-dev` version
  `2.5-2.2ubuntu3`.
- je: The [_jemalloc_](https://github.com/jemalloc/jemalloc)
  allocator by Jason Evans is developed at Facebook
  and widely used in practice, for example in FreeBSD and Firefox.
  Using version tag 5.2.0.
- sn: The [_snmalloc_](https://github.com/microsoft/snmalloc) allocator
  is a recent concurrent message passing
  allocator by Liétar et al. \[8]. Using `git-0b64536b`.
- rp: The [_rpmalloc_](https://github.com/rampantpixels/rpmalloc) allocator
   uses 32-byte aligned allocations and is developed by Mattias Jansson at Rampant Pixels.
   Using version tag 1.3.1.
- hd: The [_Hoard_](https://github.com/emeryberger/Hoard) allocator by
  Emery Berger \[1]. This is one of the first
  multi-thread scalable allocators. Using version tag 3.13.
- glibc: The system allocator. Here we use the _glibc_ allocator (which is originally based on
  _Ptmalloc2_), using version 2.27.0. Note that version 2.26 significantly improved scalability over
  earlier versions.
- sm: The [_Supermalloc_](https://github.com/kuszmaul/SuperMalloc) allocator by
  Bradley Kuszmaul uses hardware transactional memory
  to speed up parallel operations. Using version `git-709663fb`.
- tbb: The Intel [TBB](https://github.com/intel/tbb) allocator that comes with
  the Thread Building Blocks (TBB) library \[7].
  Installed as package `libtbb-dev`, version `2017~U7-8`.

All allocators run exactly the same benchmark programs on Ubuntu 18.04.1
and use `LD_PRELOAD` to override the default allocator. The wall-clock
elapsed time and peak resident memory (_rss_) are measured with the
`time` program. The average scores over 5 runs are used. Performance is
reported relative to _mimalloc_, e.g. a time of 1.5&times; means that
the program took 1.5&times; longer than _mimalloc_.

[_snmalloc_]: https://github.com/Microsoft/_snmalloc_
[_rpmalloc_]: https://github.com/rampantpixels/_rpmalloc_


## Benchmarks

The first set of benchmarks are real world programs and consist of:

- __cfrac__: by Dave Barrett, implementation of continued fraction factorization which
  uses many small short-lived allocations -- exactly the workload
  we are targeting for Koka and Lean.   
- __espresso__: a programmable logic array analyzer, described by
  Grunwald, Zorn, and Henderson \[3]. in the context of cache aware memory allocation.
- __barnes__: a hierarchical n-body particle solver \[4] which uses relatively few
  allocations compared to `cfrac` and `espresso`. Simulates the gravitational forces
  between 163840 particles.
- __leanN__:  The [Lean](https://github.com/leanprover/lean) compiler by
  de Moura _et al_, version 3.4.1,
  compiling its own standard library concurrently using N threads
  (`./lean --make -j N`). Big real-world workload with intensive
  allocation.
- __redis__: running the [redis](https://redis.io/) 5.0.3 server on
  1 million requests pushing 10 new list elements and then requesting the
  head 10 elements. Measures the requests handled per second.
- __larsonN__: by Larson and Krishnan \[2]. Simulates a server workload using 100 separate
   threads which each allocate and free many objects but leave some
   objects to be freed by other threads. Larson and Krishnan observe this
   behavior (which they call _bleeding_) in actual server applications,
   and the benchmark simulates this.

The second set of  benchmarks are stress tests and consist of:

- __alloc-test__: a modern allocator test developed by
  OLogN Technologies AG ([ITHare.com](http://ithare.com/testing-memory-allocators-ptmalloc2-tcmalloc-hoard-jemalloc-while-trying-to-simulate-real-world-loads/))
  Simulates intensive allocation workloads with a Pareto size
  distribution. The _alloc-testN_ benchmark runs on N cores doing
  100&middot;10^6^ allocations per thread with objects up to 1KiB
  in size. Using commit `94f6cb`
  ([master](https://github.com/node-dot-cpp/alloc-test), 2018-07-04)
- __sh6bench__: by [MicroQuill](http://www.microquill.com/) as part of SmartHeap. Stress test
   where some of the objects are freed in a
   usual last-allocated, first-freed (LIFO) order, but others are freed
   in reverse order. Using the
   public [source](http://www.microquill.com/smartheap/shbench/bench.zip)
   (retrieved 2019-01-02)
- __sh8benchN__: by [MicroQuill](http://www.microquill.com/) as part of SmartHeap. Stress test for
  multi-threaded allocation (with N threads) where, just as in _larson_,
  some objects are freed by other threads, and some objects freed in
  reverse (as in _sh6bench_). Using the
  public [source](http://www.microquill.com/smartheap/SH8BENCH.zip)
  (retrieved 2019-01-02)
- __xmalloc-testN__: by Lever and Boreham \[5] and Christian Eder. We use the updated
  version from the SuperMalloc repository. This is a more
  extreme version of the _larson_ benchmark with 100 purely allocating threads,
  and 100 purely deallocating threads with objects of various sizes migrating
  between them. This asymmetric producer/consumer pattern is usually difficult
  to handle by allocators with thread-local caches.
- __cache-scratch__: by Emery Berger \[1]. Introduced with the Hoard
  allocator to test for _passive-false_ sharing of cache lines: first
  some small objects are allocated and given to each thread; the threads
  free that object and allocate immediately another one, and access that
  repeatedly. If an allocator allocates objects from different threads
  close to each other this will lead to cache-line contention.


## On a 16-core AMD EPYC running Linux

Testing on a big Amazon EC2 instance ([r5a.4xlarge](https://aws.amazon.com/ec2/instance-types/))
consisting of a 16-core AMD EPYC 7000 at 2.5GHz
with 128GB ECC memory, running	Ubuntu 18.04.1 with LibC 2.27 and GCC 7.3.0.
We excluded SuperMalloc here as it use transactional memory instructions
that are usually not supported in a virtualized environment.

![bench-r5a-1](doc/bench-r5a-1.svg)
![bench-r5a-2](doc/bench-r5a-2.svg)

Memory usage:

![bench-r5a-rss-1](doc/bench-r5a-rss-1.svg)
![bench-r5a-rss-1](doc/bench-r5a-rss-2.svg)

(note: the _xmalloc-testN_ memory usage should be disregarded is it
allocates more the faster the program runs).

In the first five benchmarks we can see _mimalloc_ outperforms the other
allocators moderately, but we also see that all these modern allocators
perform well -- the times of large performance differences in regular
workloads are over. In
_cfrac_ and _espresso_, _mimalloc_ is a tad faster than _tcmalloc_ and
_jemalloc_, but a solid 10\% faster than all other allocators on
_espresso_. The _tbb_ allocator does not do so well here and lags more than
20\% behind _mimalloc_. The _cfrac_ and _espresso_ programs do not use much
memory (~1.5MB) so it does not matter too much, but still _mimalloc_ uses
about half the resident memory of _tcmalloc_.

The _leanN_ program is most interesting as a large realistic and
concurrent workload and there is a 8% speedup over _tcmalloc_. This is
quite significant: if Lean spends 20% of its time in the
allocator that means that _mimalloc_ is 1.3&times; faster than _tcmalloc_
here. This is surprising as that is *not* measured in a pure
allocation benchmark like _alloc-test_. We conjecture that we see this
outsized improvement here because _mimalloc_ has better locality in
the allocation which improves performance for the *other* computations
in a program as well.

The _redis_ benchmark shows more differences between the allocators where
_mimalloc_ is 14\% faster than _jemalloc_. On this benchmark _tbb_ (and _Hoard_) do
not do well and are over 40\% slower.

The _larson_ server workload which allocates and frees objects between
many threads shows even larger differences, where _mimalloc_ is more than
2.5&times; faster than _tcmalloc_ and _jemalloc_ which is quite surprising
for these battle tested allocators -- probably due to the object
migration between different threads. This is a difficult benchmark for
other allocators too where _mimalloc_ is still 48% faster than the next
fastest (_snmalloc_).


The second benchmark set tests specific aspects of the allocators and
shows even more extreme differences between them.

The _alloc-test_ is very allocation intensive doing millions of
allocations in various size classes. The test is scaled such that when an
allocator performs almost identically on _alloc-test1_ as _alloc-testN_ it
means that it scales linearly. Here, _tcmalloc_, _snmalloc_, and
_Hoard_ seem to scale less well and do more than 10% worse on the
multi-core version. Even the best allocators (_tcmalloc_ and _jemalloc_) are
more than 10% slower as _mimalloc_ here.

Also in _sh6bench_ _mimalloc_ does much
better than the others (more than 2&times; faster than _jemalloc_).
We cannot explain this well but believe it is
caused in part by the "reverse" free-ing pattern in _sh6bench_.

Again in _sh8bench_ the _mimalloc_ allocator handles object migration
between threads much better and is over 36% faster than the next best
allocator, _snmalloc_. Whereas _tcmalloc_ did well on _sh6bench_, the
addition of object migration caused it to be almost 3 times slower
than before.

The _xmalloc-testN_ benchmark simulates an asymmetric workload where
some threads only allocate, and others only free. The _snmalloc_
allocator was especially developed to handle this case well as it
often occurs in concurrent message passing systems. Here we see that
the _mimalloc_ technique of having  non-contended sharded thread free
lists pays off and it even outperforms _snmalloc_. Only _jemalloc_
also handles this reasonably well, while the others underperform by
a large margin. The optimization on _mimalloc_ to do a *delayed free*
only once for full pages is quite important -- without it _mimalloc_
is almost twice as slow (as then all frees contend again on the
single heap delayed free list).


The _cache-scratch_ benchmark also demonstrates the different
architectures of the allocators nicely. With a single thread they all
perform the same, but when running with multiple threads the allocator
induced false sharing of the cache lines causes large run-time
differences, where _mimalloc_ is more than 18&times; faster than _jemalloc_ and
_tcmalloc_! Crundal \[6] describes in detail why the false cache line
sharing occurs in the _tcmalloc_ design, and also discusses how this
can be avoided with some small implementation changes.
Only _snmalloc_ and _tbb_ also avoid the
cache line sharing like _mimalloc_. Kukanov and Voss \[7] describe in detail
how the design of _tbb_ avoids the false cache line sharing.
The _Hoard_ allocator is also specifically
designed to avoid this false sharing and we are not sure why it is not
doing well here (although it runs still 5&times; as fast as _tcmalloc_).



## On a 4-core Intel Xeon workstation

Below are the benchmark results on an HP
Z4-G4 workstation with a 4-core Intel® Xeon® W2123 at 3.6 GHz with 16GB
ECC memory, running Ubuntu 18.04.1 with LibC 2.27 and GCC 7.3.0.

![bench-z4-1](doc/bench-z4-1.svg)
![bench-z4-2](doc/bench-z4-2.svg)

Memory usage:

![bench-z4-rss-1](doc/bench-z4-rss-1.svg)
![bench-z4-rss-2](doc/bench-z4-rss-2.svg)

(note: the _xmalloc-testN_ memory usage should be disregarded is it
allocates more the faster the program runs).

This time SuperMalloc (_sm_) is included as this platform supports
hardware transactional memory. Unfortunately,
there are no entries for _SuperMalloc_ in the _leanN_ and _xmalloc-testN_ benchmarks
as it faulted on those. We also added the secure version of
_mimalloc_ as smi.

Overall, the relative results are quite similar as before. Most
allocators fare better on the _larsonN_ benchmark now -- either due to
architectural changes (AMD vs. Intel) or because there is just less
concurrency. Unfortunately, the SuperMalloc faulted on the _leanN_
and _xmalloc-testN_ benchmarks.

The secure mimalloc version uses guard pages around each (_mimalloc_) page,
encodes the free lists and uses randomized initial free lists, and we
expected it would perform quite a bit worse -- but on the first benchmark set
it performed only about 3% slower on average, and is second best overall.

-->

# References

- \[1] Emery D. Berger, Kathryn S. McKinley, Robert D. Blumofe, and Paul R. Wilson.
   _Hoard: A Scalable Memory Allocator for Multithreaded Applications_
   the Ninth International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS-IX). Cambridge, MA, November 2000.
   [pdf](http://www.cs.utexas.edu/users/mckinley/papers/asplos-2000.pdf)


- \[2] P. Larson and M. Krishnan. _Memory allocation for long-running server applications_. In ISMM, Vancouver, B.C., Canada, 1998.
      [pdf](http://citeseemi.ist.psu.edu/viewdoc/download;jsessionid=5F0BFB4F57832AEB6C11BF8257271088?doi=10.1.1.45.1947&rep=rep1&type=pdf)

- \[3] D. Grunwald, B. Zorn, and R. Henderson.
  _Improving the cache locality of memory allocation_. In R. Cartwright, editor,
  Proceedings of the Conference on Programming Language Design and Implementation, pages 177–186, New York, NY, USA, June 1993.
  [pdf](http://citeseemi.ist.psu.edu/viewdoc/download?doi=10.1.1.43.6621&rep=rep1&type=pdf)

- \[4] J. Barnes and P. Hut. _A hierarchical O(n*log(n)) force-calculation algorithm_. Nature, 324:446-449, 1986.

- \[5] C. Lever, and D. Boreham. _Malloc() Performance in a Multithreaded Linux Environment._
  In USENIX Annual Technical Conference, Freenix Session. San Diego, CA. Jun. 2000.
  Available at <https://​github.​com/​kuszmaul/​SuperMalloc/​tree/​master/​tests>

- \[6] Timothy Crundal. _Reducing Active-False Sharing in TCMalloc._
   2016. <http://​courses.​cecs.​anu.​edu.​au/​courses/​CSPROJECTS/​16S1/​Reports/​Timothy*​Crundal*​Report.​pdf>. CS16S1 project at the Australian National University.

- \[7] Alexey Kukanov, and Michael J Voss.
   _The Foundations for Scalable Multi-Core Software in Intel Threading Building Blocks._
   Intel Technology Journal 11 (4). 2007

- \[8] Paul Liétar, Theodore Butler, Sylvan Clebsch, Sophia Drossopoulou, Juliana Franco, Matthew J Parkinson,
  Alex Shamis, Christoph M Wintersteiger, and David Chisnall.
  _Snmalloc: A Message Passing Allocator._
  In Proceedings of the 2019 ACM SIGPLAN International Symposium on Memory Management, 122–135. ACM. 2019.
