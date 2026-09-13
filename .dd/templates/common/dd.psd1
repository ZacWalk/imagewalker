@{
    schema = 1
    project = @{
        name = '@NAME@'
        type = '@TYPE@'
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
            id = 'app'
            kind = '@TYPE@'
            'cmake-target' = 'app'
            'test-label' = 'app'
            'debug-path' = 'build/{platform}/debug/bin/app{exe}'
            'release-path' = 'build/{platform}/release/bin/app{exe}'
        }
    )
}