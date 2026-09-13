@{
    schema = 1
    project = @{
        name = 'imagewalker'
        type = 'gui'
        'default-target' = 'iw30'
    }
    build = @{
        'x64-windows' = @{
            debug = 'x64-debug'
            release = 'x64-release'
            ide = 'vs'
        }
    }
    dependencies = @{ owner = 'application' }
    requirements = @{
        tools = @{
            cmake = @{ minimum = '3.28.0' }
            python = @{ minimum = '3.9.0' }
        }
        msvc = @{
            minimum = '18.0'
            components = @('Microsoft.VisualStudio.Component.VC.ATL')
        }
    }
    targets = @(
        @{
            id = 'iw10'
            'test-label' = 'iw10'
            kind = 'gui'
            'cmake-target' = 'imagewalker10'
            'debug-path' = 'exe/imagewalker10d.exe'
            'release-path' = 'exe/imagewalker10.exe'
        }
        @{
            id = 'iw20'
            'test-label' = 'iw20'
            kind = 'gui'
            'cmake-target' = 'imagewalker20'
            'debug-path' = 'exe/imagewalker20d.exe'
            'release-path' = 'exe/imagewalker20.exe'
        }
        @{
            id = 'iw22'
            'test-label' = 'iw22'
            kind = 'gui'
            'cmake-target' = 'imagewalker22'
            'debug-path' = 'exe/imagewalker22d.exe'
            'release-path' = 'exe/imagewalker22.exe'
        }
        @{
            id = 'iw23'
            'test-label' = 'iw23'
            kind = 'gui'
            'cmake-target' = 'imagewalker23'
            'debug-path' = 'exe/imagewalker23d.exe'
            'release-path' = 'exe/imagewalker23.exe'
        }
        @{
            id = 'iw30'
            'test-label' = 'iw30'
            kind = 'gui'
            'cmake-target' = 'imagewalker30'
            'debug-path' = 'exe/imagewalker30d.exe'
            'release-path' = 'exe/imagewalker30.exe'
        }
    )
    commands = @{
        'check-help' = @{
            description = 'Validate help links and strictly rebuild the CHM'
            script = 'tools/dd-project.ps1'
            effects = 'write'
            'supports-dry-run' = $true
            'timeout-secs' = 600
            platforms = @('x64-windows')
        }
        'test-app' = @{
            description = 'Build both configurations and run one application CTest suite'
            script = 'tools/dd-project.ps1'
            effects = 'write'
            'supports-dry-run' = $true
            'timeout-secs' = 1800
            platforms = @('x64-windows')
            parameters = @{
                app = @{
                    type = 'string'
                    choices = @('iw10', 'iw20', 'iw22', 'iw23', 'iw30')
                    default = 'iw30'
                }
            }
        }
    }
}