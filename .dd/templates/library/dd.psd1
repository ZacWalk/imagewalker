@{
    schema = 1
    project = @{
        name = '@NAME@'
        type = 'library'
        'default-target' = 'app'
    }
    build = @{
        'x64-windows' = @{
            debug = 'windows-debug'
            release = 'windows-release'
            ide = 'windows-ide'
        }
        'x64-linux' = @{
            debug = 'linux-debug'
            release = 'linux-release'
        }
    }
    targets = @(
        @{
            id = 'lib'
            kind = 'library'
            'cmake-target' = 'applib'
            'test-label' = 'lib'
            'debug-path' = 'build/{platform}/debug/lib/{libprefix}applib{lib}'
            'release-path' = 'build/{platform}/release/lib/{libprefix}applib{lib}'
        },
        @{
            id = 'app'
            kind = 'cli'
            'cmake-target' = 'app'
            'test-label' = 'app'
            'debug-path' = 'build/{platform}/debug/bin/app{exe}'
            'release-path' = 'build/{platform}/release/bin/app{exe}'
        }
    )
}
