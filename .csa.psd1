# docs/warnings.md limits analysis to the modern application and shared imaging code.
@{ Build='build/x64-debug'; Paths=@('src30','src/shared','include','tests',':!include/wtl',':!include/iw/legacy',':!include/iw/ColorButton.h',':!tests/imaging/legacy.cpp',':!tests/imaging/header-legacy-*.cpp',':!tests/native-test-windows.cpp') }
