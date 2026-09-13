#include <string_view>

int main()
{
    constexpr std::string_view name = "@NAME@";
    return name.empty() ? 1 : 0;
}