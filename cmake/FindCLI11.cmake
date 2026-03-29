include(FetchContent)
FetchContent_Declare(
    cli11_proj
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG 37bb6edc5317e99af72ef48405e65d9ca5218861 #v2.6.2
)
FetchContent_MakeAvailable(cli11_proj)