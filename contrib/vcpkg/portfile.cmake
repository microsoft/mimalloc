vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO microsoft/mimalloc
  HEAD_REF master

  # The "REF" can be a commit hash, branch name (dev2), or a version (v2.2.1).
  # REF "v${VERSION}"
  REF 191ea046e4213e1a59652b3f4975219115ce1bed

  # The sha512 is the hash of the tar.gz bundle.
  # (To get the sha512, run `vcpkg install mimalloc[override] --overlay-ports=<dir of this file>` and copy the sha from the error message.)
  SHA512 d35926d368eb89b1688fafe22192c44e349ae78553a3a8def78bca847adff8a29e388a92027f03bd4fb6d6b4c906c70e9fd962bd539fbc8ef383fac95f64d8cd
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
  FEATURES
    c           MI_NO_USE_CXX
    guarded     MI_GUARDED
    secure      MI_SECURE
    override    MI_OVERRIDE
    xmalloc     MI_XMALLOC
    asm         MI_SEE_ASM
)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" MI_BUILD_STATIC)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" MI_BUILD_SHARED)

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS_RELEASE
    -DMI_OPT_ARCH=ON
  OPTIONS
    -DMI_USE_CXX=ON
    -DMI_BUILD_TESTS=OFF
    -DMI_BUILD_OBJECT=ON
    ${FEATURE_OPTIONS}
    -DMI_BUILD_STATIC=${MI_BUILD_STATIC}
    -DMI_BUILD_SHARED=${MI_BUILD_SHARED}
    -DMI_INSTALL_TOPLEVEL=ON
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

file(COPY
  "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake"
  "${CMAKE_CURRENT_LIST_DIR}/usage"
  DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/mimalloc)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
  # todo: why is this needed?
  vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/include/mimalloc.h"
    "!defined(MI_SHARED_LIB)"
    "0 // !defined(MI_SHARED_LIB)"
  )
endif()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_fixup_pkgconfig()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
