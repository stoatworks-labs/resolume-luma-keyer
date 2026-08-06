# The FxPlug 4 build, for Final Cut Pro and Motion.
#
# Two products, because FxPlug demands both:
#
#   Stoatworks Luma Key.app          the wrapper - an empty app whose only job is
#     Contents/                      to exist in /Applications so that the system
#       Frameworks/                  registers the service inside it
#         FxPlug.framework           embedded and re-signed; the SDK's copy under
#         PluginManager.framework    /Library/Developer/Frameworks is a developer
#       PlugIns/                     install and is not on a user's machine
#         LumaKey.fxplug             the XPC service that actually renders
#
# Apple's Xcode template does this with custom product types
# (com.apple.fxplug.wrapperAppTarget / .xpcTarget) that are not installed as
# xcspecs on this machine, so the layout is assembled here by hand instead. That
# also keeps the build in the same CMake the FFGL and OpenFX products use, which
# is what lets the shared key in source/LumaKeyCore.h link straight in.

set(FXPLUG_SDK "/Library/Developer/SDKs/FxPlug.sdk"
    CACHE PATH "Apple's FxPlug SDK (a login-gated download; not redistributable)")
set(FXPLUG_FRAMEWORKS "/Library/Developer/Frameworks"
    CACHE PATH "Runtime FxPlug.framework and PluginManager.framework")

if(NOT EXISTS "${FXPLUG_SDK}")
    message(FATAL_ERROR
        "FxPlug SDK not found at ${FXPLUG_SDK}.\n"
        "It is a login-gated download from developer.apple.com and cannot be "
        "vendored into this repo, so CI cannot build this target - see "
        "docs/FXPLUG-PORT.md.")
endif()

set(FXPLUG_APP_NAME "Stoatworks Luma Key")
set(FXPLUG_SERVICE_NAME "LumaKey")

# ---------------------------------------------------------------- XPC service

add_executable(LumaKeyFxPlug
    LumaKeyPlugIn.mm
    main.mm
)

set_target_properties(LumaKeyFxPlug PROPERTIES
    OUTPUT_NAME "${FXPLUG_SERVICE_NAME}"
)

target_include_directories(LumaKeyFxPlug PRIVATE
    "${CMAKE_SOURCE_DIR}/source"
)

# The SDK is a sparse SDK: headers and .tbd stubs only. Frameworks are found
# there at compile time and at @rpath at run time. Note the -F path is passed
# unquoted — CMake escapes embedded quotes into the flag, and clang then looks
# for a directory whose name literally starts with a double quote.
target_compile_options(LumaKeyFxPlug PRIVATE
    -F${FXPLUG_SDK}/Library/Frameworks
    -fobjc-arc
)
target_link_options(LumaKeyFxPlug PRIVATE
    -F${FXPLUG_SDK}/Library/Frameworks
    # Resolved relative to the service binary, up into the wrapper app:
    #   .app/Contents/PlugIns/LumaKey.fxplug/Contents/MacOS/  ->  .app/Contents/Frameworks
    "-Wl,-rpath,@loader_path/../../../../Frameworks"
)
# Each "-framework X" is ONE quoted item on purpose. Listed as separate options
# CMake de-duplicates the repeated -framework flag and the linker is left with a
# bare framework name it reads as a missing file.
target_link_libraries(LumaKeyFxPlug PRIVATE
    "-framework FxPlug"
    "-framework PluginManager"
    "-framework Foundation"
    "-framework AppKit"
    "-framework CoreMedia"
    "-framework IOSurface"
)

configure_file(Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/ServiceInfo.plist" @ONLY)
configure_file(wrapper/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/WrapperInfo.plist" @ONLY)

# ------------------------------------------------------------- wrapper binary

add_executable(LumaKeyFxPlugWrapper wrapper/main.mm)
set_target_properties(LumaKeyFxPlugWrapper PROPERTIES
    OUTPUT_NAME "${FXPLUG_APP_NAME}"
)
target_compile_options(LumaKeyFxPlugWrapper PRIVATE -fobjc-arc)
target_link_libraries(LumaKeyFxPlugWrapper PRIVATE "-framework Cocoa")

# --------------------------------------------------------------------- tests
#
# Host-free: drives LumaKeyTile.h directly, so it needs neither the SDK nor a
# running Final Cut Pro. `ctest --test-dir build-fxplug` or run the binary.

add_executable(lumakey-tiletest test/tiletest.cpp)
target_include_directories(lumakey-tiletest PRIVATE "${CMAKE_SOURCE_DIR}/source")
enable_testing()
add_test(NAME tiletest COMMAND lumakey-tiletest)

# ------------------------------------------------------------------ assembly

set(FXPLUG_APP "${CMAKE_BINARY_DIR}/${FXPLUG_APP_NAME}.app")
# .pluginkit, NOT .fxplug. The extension is what pluginkit keys discovery off:
# with .fxplug the app installs, signs and verifies perfectly and then simply
# never appears in `pluginkit -m -p FxPlug`, with no error anywhere. Motion's own
# service (Contents/PlugIns/InternalFiltersXPC.pluginkit) is the reference.
set(FXPLUG_SERVICE "${FXPLUG_APP}/Contents/PlugIns/${FXPLUG_SERVICE_NAME}.pluginkit")

add_custom_target(LumaKeyFxPlugBundle ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${FXPLUG_APP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FXPLUG_APP}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FXPLUG_APP}/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FXPLUG_SERVICE}/Contents/MacOS"

    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/WrapperInfo.plist" "${FXPLUG_APP}/Contents/Info.plist"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:LumaKeyFxPlugWrapper>" "${FXPLUG_APP}/Contents/MacOS/"

    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/ServiceInfo.plist" "${FXPLUG_SERVICE}/Contents/Info.plist"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:LumaKeyFxPlug>" "${FXPLUG_SERVICE}/Contents/MacOS/"

    # Embedded rather than referenced: /Library/Developer/Frameworks is part of
    # the SDK install and is absent on a user's machine. Headers are excluded
    # because shipping Apple's headers is not ours to do.
    COMMAND rsync --archive --links --exclude=Headers --exclude=PrivateHeaders
        "${FXPLUG_FRAMEWORKS}/FxPlug.framework" "${FXPLUG_APP}/Contents/Frameworks/"
    COMMAND rsync --archive --links --exclude=Headers --exclude=PrivateHeaders
        "${FXPLUG_FRAMEWORKS}/PluginManager.framework" "${FXPLUG_APP}/Contents/Frameworks/"

    DEPENDS LumaKeyFxPlug LumaKeyFxPlugWrapper
    COMMENT "Assembling ${FXPLUG_APP_NAME}.app"
    VERBATIM
)
