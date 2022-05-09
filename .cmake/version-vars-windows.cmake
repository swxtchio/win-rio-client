execute_process (
    COMMAND "${GIT_EXECUTABLE}" describe --abbrev=0 --tags
    OUTPUT_VARIABLE SWXTCH_BUILD_VERSION
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --abbrev=40 --dirty
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE SWXTCH_BUILD_COMMIT
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process (
    COMMAND CMD /C "${RIO_HOME}\\.github\\scripts\\getFullDateTime.bat"
    OUTPUT_VARIABLE SWXTCH_BUILD_DATE
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)