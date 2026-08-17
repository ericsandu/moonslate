# Included via CMAKE_PROJECT_INCLUDE for iOS builds. sentencepiece 0.2.0 calls
# set_xcode_property() when CMAKE_SYSTEM_NAME is iOS, but that helper is only
# defined for Xcode-generator setups; under Ninja the call is a fatal "Unknown
# CMake command". The properties it sets are advisory for the Xcode IDE, so a
# no-op definition is sufficient.
if(NOT COMMAND set_xcode_property)
    function(set_xcode_property)
    endfunction()
endif()
