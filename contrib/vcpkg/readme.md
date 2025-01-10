# Vcpkg support

This directory is meant to provide the sources for the official [vcpkg port] 
of mimalloc, but can also be used to override the official port with
your own variant.

For example, you can edit the [`portfile.cmake`](portfile.cmake) 
to check out a specific commit, version, or branch of mimalloc, or set further options. 
You can install such custom port as:
```sh
$ vcpkg install mimalloc --overlay-ports=./contrib/vcpkg
```

This will also show the correct sha512 hash if you use a custom version.
See the vcpkg [documentation](https://learn.microsoft.com/en-us/vcpkg/produce/update-package-version) for more information.


# Using mimalloc from vcpkg

When using [cmake with vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell), 
you can use mimalloc from the `CMakeLists.txt` as:

```cmake
find_package(mimalloc CONFIG REQUIRED)
target_link_libraries(main PRIVATE mimalloc)
```


# Acknowledgements

The original port for vckpg was contributed by various people, including: @vicroms, @myd7349, @PhoubeHui, @LilyWangL,
@JonLiu1993, @RT2Code, Remy Tassoux, @wangao, @BillyONeal, @jiayuehua, @dg0yt, @gerar-ryan-immersaview, @nickdademo, 
and @jimwang118 -- Thank you so much!


[vcpkg port]: https://github.com/microsoft/vcpkg/tree/master/ports/mimalloc
