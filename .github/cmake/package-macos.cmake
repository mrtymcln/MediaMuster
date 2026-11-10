cmake_minimum_required(VERSION 3.31)

# Run from the repository root after building the Mac application.
foreach(required APP_VERSION APPLE_TEAM_ID APPLE_ID_USERNAME APPLE_ID_PASSWORD)
    if("$ENV{${required}}" STREQUAL "")
        message(FATAL_ERROR "Mac distribution requires ${required}; packaging stopped")
    endif()
endforeach()

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

# Use the app's configured identity.
load_cache("${buildDir}" READ_WITH_PREFIX signing_ MEDIAMUSTER_CODESIGN_IDENTITY)
set(identity "${signing_MEDIAMUSTER_CODESIGN_IDENTITY}")
if(NOT identity MATCHES "^Developer ID Application: .+ \\(([A-Z0-9]+)\\)$")
    message(FATAL_ERROR "Configure a Developer ID Application signing identity")
endif()
if(NOT "${CMAKE_MATCH_1}" STREQUAL "$ENV{APPLE_TEAM_ID}")
    message(FATAL_ERROR "Signing identity does not match APPLE_TEAM_ID")
endif()
execute_process(
    COMMAND security find-identity -v -p codesigning
    OUTPUT_VARIABLE identities
    ERROR_QUIET
    RESULT_VARIABLE result)
checkResult("${result}" "Reading signing identities failed")
string(FIND "${identities}" "\"${identity}\"" identityPosition)
if(identityPosition EQUAL -1)
    message(FATAL_ERROR "Configured Developer ID identity is unavailable; packaging stopped")
endif()
set(signArgs --force --options runtime --timestamp --sign "${identity}")

runChecked("Qt deployment failed" macdeployqt "${app}")

# Sign nested components before the app.
file(GLOB frameworkBundles "${frameworks}/*.framework")
foreach(component IN LISTS frameworkBundles)
    runChecked("Framework signing failed" codesign ${signArgs} "${component}")
endforeach()
file(GLOB_RECURSE pluginLibraries LIST_DIRECTORIES FALSE "${plugins}/*.dylib")
foreach(component IN LISTS pluginLibraries)
    runChecked("Plugin signing failed" codesign ${signArgs} "${component}")
endforeach()
runChecked("Application signing failed" codesign ${signArgs} "${app}")
runChecked("Application signature verification failed"
    codesign --verify --deep --strict --verbose=2 "${app}")
runChecked("Flushing signed application failed" sync)
runChecked("Waiting for signed application failed" "${CMAKE_COMMAND}" -E sleep 2)

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
runChecked("Copying application to DMG staging failed" ditto "${app}" "${staging}/MediaMuster.app")
runChecked("Creating Applications shortcut failed"
    "${CMAKE_COMMAND}" -E create_symlink /Applications "${staging}/Applications")
runChecked("Flushing DMG staging failed" sync)
runChecked("Waiting for DMG staging failed" "${CMAKE_COMMAND}" -E sleep 4)
runChecked("Removing previous DMG failed" "${CMAKE_COMMAND}" -E rm -f "${dmg}")
runChecked("Creating DMG failed"
    hdiutil create -volname MediaMuster -srcfolder "${staging}" -ov -format ULMO "${dmg}")

runChecked("Disk image signing failed"
    codesign --force --timestamp --sign "${identity}"
        --identifier com.McLean.MediaMuster.dmg "${dmg}")
runChecked("Disk image signature verification failed"
    codesign --verify --strict --verbose=2 "${dmg}")

# Notarise the container and its enclosed app together.
execute_process(
    COMMAND xcrun notarytool submit "${dmg}"
        --apple-id "$ENV{APPLE_ID_USERNAME}"
        --password "$ENV{APPLE_ID_PASSWORD}"
        --team-id "$ENV{APPLE_TEAM_ID}" --wait --output-format json
    COMMAND_ECHO NONE
    WORKING_DIRECTORY "${buildDir}"
    OUTPUT_VARIABLE notarization
    ERROR_VARIABLE notarizationError
    RESULT_VARIABLE result)
checkResult("${result}" "Notarisation submission failed")
string(JSON status ERROR_VARIABLE parseError GET "${notarization}" status)
if(parseError)
    checkResult(1 "Notarisation returned no valid status")
endif()
if(NOT status STREQUAL "Accepted")
    string(JSON submissionId ERROR_VARIABLE idError GET "${notarization}" id)
    checkResult(1 "Notarisation ${status} (submission ${submissionId}); inspect Apple's notary log")
endif()
runChecked("Stapling notarisation ticket failed" xcrun stapler staple "${dmg}")
runChecked("Validating notarisation ticket failed" xcrun stapler validate "${dmg}")
runChecked("Removing temporary DMG staging failed" "${CMAKE_COMMAND}" -E rm -rf "${staging}")
