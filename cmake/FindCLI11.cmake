include(FetchContent)
FetchContent_Declare(
    cli11_proj
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG 291c58789c031208f08f4f261a858b5b7083e8e2 #v2.3.2
)
FetchContent_MakeAvailable(cli11_proj)