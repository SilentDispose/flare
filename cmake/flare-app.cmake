function(FlareApp target)
    set(embedScript "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed-web-assets.cmake")
    if(NOT EXISTS "${embedScript}")
        message(FATAL_ERROR "FlareApp cannot find embed-web-assets.cmake next to its module")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "FlareApp requires an existing target: ${target}")
    endif()
    if(TARGET flare)
        target_link_libraries(${target} PRIVATE flare)
    elseif(TARGET Flare::flare)
        target_link_libraries(${target} PRIVATE Flare::flare)
    else()
        message(FATAL_ERROR "FlareApp requires the flare target; call find_package(Flare) or add_subdirectory first")
    endif()
    if(ARGC GREATER 1)
        set(webDir "${ARGV1}")
        if(NOT IS_ABSOLUTE "${webDir}")
            set(webDir "${CMAKE_CURRENT_SOURCE_DIR}/${webDir}")
        endif()
        if(NOT IS_DIRECTORY "${webDir}")
            message(FATAL_ERROR "FlareApp web directory does not exist: ${webDir}")
        endif()
        file(GLOB_RECURSE webFiles CONFIGURE_DEPENDS RELATIVE "${webDir}" "${webDir}/*")
        list(SORT webFiles)
        if(NOT webFiles)
            message(FATAL_ERROR "FlareApp web directory contains no files: ${webDir}")
        endif()
        set(assetDir "${CMAKE_CURRENT_BINARY_DIR}/flare-web-assets")
        file(MAKE_DIRECTORY "${assetDir}")
        set(manifestFile "${assetDir}/${target}.manifest")
        set(manifestContent "")
        set(webAssetDeps "")
        foreach(webFile IN LISTS webFiles)
            string(APPEND manifestContent "${webFile}\n")
            list(APPEND webAssetDeps "${webDir}/${webFile}")
        endforeach()
        file(WRITE "${manifestFile}" "${manifestContent}")
        set(generatedSource "${assetDir}/${target}-web-assets.cpp")
        add_custom_command(
            OUTPUT "${generatedSource}"
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT_DIR=${webDir}"
                "-DOUTPUT_FILE=${generatedSource}"
                "-DBUNDLE_NAME=${target}"
                "-DMANIFEST_FILE=${manifestFile}"
                -P "${embedScript}"
            DEPENDS ${webAssetDeps} "${manifestFile}" "${embedScript}"
            COMMENT "Embedding web assets for ${target}"
            VERBATIM
        )
        target_sources(${target} PRIVATE "${generatedSource}")
    endif()
endfunction()
