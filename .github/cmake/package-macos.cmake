cmake_minimum_required(VERSION 3.31)

# Run from the repository root after building the Mac application.
if("$ENV{APP_VERSION}" STREQUAL "")
    message(FATAL_ERROR "Set APP_VERSION to the handwritten app version")
endif()

set(buildDir "${CMAKE_CURRENT_BINARY_DIR}/build")
set(app "${buildDir}/MediaMuster.app")
set(plugins "${app}/Contents/PlugIns")
set(frameworks "${app}/Contents/Frameworks")
set(dmg "${buildDir}/MediaMuster-$ENV{APP_VERSION}-Mac.dmg")

# CMake script mode has no exit trap. Every fallible command after mktemp
# passes through this check so only this invocation's staging directory is removed.
function(checkResult result description)
    if("${result}" STREQUAL "0")
        return()
    endif()
    if(DEFINED staging AND NOT "${staging}" STREQUAL "")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${staging}"
            RESULT_VARIABLE cleanupResult)
        if(NOT "${cleanupResult}" STREQUAL "0")
            message(WARNING "Could not remove temporary DMG staging: ${staging}")
        endif()
    endif()
    # Describe the failed step without echoing its arguments: notarisation
    # arguments contain credentials supplied through the environment.
    message(NOTICE "${description}: ${result}")
    if("${result}" MATCHES "^[0-9]+$")
        cmake_language(EXIT "${result}")
    endif()
    cmake_language(EXIT 1)
endfunction()

function(runChecked description)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${buildDir}"
        RESULT_VARIABLE result)
    checkResult("${result}" "${description}")
endfunction()

runChecked("Qt deployment failed" macdeployqt "${app}")

# Leftovers from custom icons.
foreach(gone IN ITEMS
    "${plugins}/imageformats" "${plugins}/iconengines" "${plugins}/generic"
    "${plugins}/tls" "${plugins}/networkinformation"
    "${frameworks}/QtSvg.framework" "${frameworks}/QtNetwork.framework")
    runChecked("Removing unused Qt component failed" "${CMAKE_COMMAND}" -E rm -rf "${gone}")
    if(EXISTS "${gone}" OR IS_SYMLINK "${gone}")
        message(FATAL_ERROR "${gone} survived the strip")
    endif()
endforeach()

# Inspect real executable files and dylibs, including the versioned binaries
# inside frameworks. Match find's existing permissions and symlink behaviour.
execute_process(
    COMMAND find "${app}" -type f "(" -perm -111 -o -name "*.dylib" ")"
    OUTPUT_VARIABLE binaries
    RESULT_VARIABLE result)
checkResult("${result}" "Enumerating deployed binaries failed")
string(REPLACE ";" "\\;" binaries "${binaries}")
string(REPLACE "\n" ";" binaries "${binaries}")
set(missing FALSE)
foreach(binary IN LISTS binaries)
    if("${binary}" STREQUAL "")
        continue()
    endif()
    execute_process(
        COMMAND otool -L "${binary}"
        OUTPUT_VARIABLE dependencies
        RESULT_VARIABLE auditResult
        ERROR_QUIET)
    # Non-Mach-O executable files have no Qt dependencies, as in the former audit.
    # A missing/unlaunchable audit tool is different: dependency checking did not run.
    if(NOT "${auditResult}" MATCHES "^[0-9]+$")
        checkResult("${auditResult}" "Inspecting deployed binary dependencies failed")
    endif()
    string(REGEX MATCHALL "@rpath/Qt[^ \t\r\n]+" dependencies "${dependencies}")
    list(REMOVE_DUPLICATES dependencies)
    foreach(dependency IN LISTS dependencies)
        string(REGEX REPLACE "^@rpath/" "" dependency "${dependency}")
        string(REGEX REPLACE "\\.framework.*$" "" dependency "${dependency}")
        if(NOT IS_DIRECTORY "${frameworks}/${dependency}.framework")
            message(NOTICE "MISSING framework: ${dependency} (linked by ${binary})")
            set(missing TRUE)
        endif()
    endforeach()
endforeach()
if(missing)
    message(FATAL_ERROR "Qt framework trim broke a dependency")
endif()

# No usable Developer ID identity is a supported ad-hoc packaging path.
execute_process(
    COMMAND security find-identity -v -p codesigning
    OUTPUT_VARIABLE identities
    ERROR_QUIET)
string(REGEX MATCH "[^\r\n]*Developer ID Application[^\r\n]*" identity "${identities}")
string(REGEX REPLACE [[.*"(.*)".*]] [[\1]] identity "${identity}")
set(notarize FALSE)
set(signArgs --force --sign -)
if(NOT "${identity}" STREQUAL "" AND NOT "$ENV{APPLE_TEAM_ID}" STREQUAL "")
    message(STATUS "Developer ID signing and notarisation")
    set(signArgs --force --options runtime --timestamp --sign "${identity}")
    set(notarize TRUE)
else()
    message(STATUS "Ad-hoc signing")
endif()

# Sign nested components before the app. Inventory packaged files, not sources.
file(GLOB frameworkBundles "${frameworks}/*.framework")
foreach(component IN LISTS frameworkBundles)
    runChecked("Framework signing failed" codesign ${signArgs} "${component}")
endforeach()
file(GLOB_RECURSE pluginLibraries LIST_DIRECTORIES FALSE "${plugins}/*.dylib")
foreach(component IN LISTS pluginLibraries)
    runChecked("Plugin signing failed" codesign ${signArgs} "${component}")
endforeach()
runChecked("Application signing failed" codesign ${signArgs} "${app}")
if(notarize)
    runChecked("Application signature verification failed"
        codesign --verify --deep --strict --verbose=2 "${app}")
    runChecked("Flushing signed application failed" sync)
    runChecked("Waiting for signed application failed" "${CMAKE_COMMAND}" -E sleep 2)
endif()

if(NOT "$ENV{RUNNER_TEMP}" STREQUAL "")
    set(tempRoot "$ENV{RUNNER_TEMP}")
elseif(NOT "$ENV{TMPDIR}" STREQUAL "")
    set(tempRoot "$ENV{TMPDIR}")
else()
    set(tempRoot "/tmp")
endif()
execute_process(
    COMMAND mktemp -d "${tempRoot}/mediamuster-dmg.XXXXXX"
    OUTPUT_VARIABLE createdStaging
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE result)
checkResult("${result}" "Creating temporary DMG staging failed")
set(staging "${createdStaging}")

# Preserve the app's framework symlinks and native copy semantics.
runChecked("Copying application to DMG staging failed" cp -R "${app}" "${staging}/")
runChecked("Creating Applications shortcut failed"
    "${CMAKE_COMMAND}" -E create_symlink /Applications "${staging}/Applications")
runChecked("Flushing DMG staging failed" sync)
runChecked("Waiting for DMG staging failed" "${CMAKE_COMMAND}" -E sleep 4)
runChecked("Removing previous DMG failed" "${CMAKE_COMMAND}" -E rm -f "${dmg}")
runChecked("Creating DMG failed"
    hdiutil create -volname MediaMuster -srcfolder "${staging}" -ov -format ULMO "${dmg}")

if(notarize)
    # Keep credential-bearing arguments directly quoted, without a command echo.
    execute_process(
        COMMAND xcrun notarytool submit "${dmg}"
            --apple-id "$ENV{APPLE_ID_USERNAME}"
            --password "$ENV{APPLE_ID_PASSWORD}"
            --team-id "$ENV{APPLE_TEAM_ID}" --wait
        COMMAND_ECHO NONE
        WORKING_DIRECTORY "${buildDir}"
        RESULT_VARIABLE result)
    checkResult("${result}" "Notarisation failed")
    runChecked("Stapling notarisation ticket failed" xcrun stapler staple "${dmg}")
    runChecked("Validating notarisation ticket failed" xcrun stapler validate "${dmg}")
endif()
runChecked("Removing temporary DMG staging failed" "${CMAKE_COMMAND}" -E rm -rf "${staging}")
