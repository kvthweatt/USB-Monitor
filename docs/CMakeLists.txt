# Only proceed if Doxygen was found
if(DOXYGEN_FOUND)
    # Configure the Doxyfile
    set(DOXYGEN_INPUT_DIR ${CMAKE_SOURCE_DIR}/src)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # Copy and configure the Doxyfile from the template
    configure_file(${DOXYFILE_IN} ${DOXYFILE} @ONLY)

    # Add custom target for building documentation
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)

    # Add custom target for installing documentation
    install(DIRECTORY ${DOXYGEN_OUTPUT_DIR}/html
            DESTINATION share/doc/${PROJECT_NAME})
endif()
