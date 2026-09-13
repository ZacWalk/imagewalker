#include <iostream>
#include <string_view>

int main(int count, char** arguments)
{
    if (count == 2 && std::string_view(arguments[1]) == "--help")
    {
        std::cout << "Usage: @NAME@ [--help]\n";
        return 0;
    }
    if (count > 1)
    {
        std::cerr << "Unknown argument. Use --help.\n";
        return 2;
    }
    std::cout << "@NAME@\n";
    return 0;
}