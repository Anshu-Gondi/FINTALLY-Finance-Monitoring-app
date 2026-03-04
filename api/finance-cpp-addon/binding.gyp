{
  "targets": [
    {
      "target_name": "finance",
      "sources": ["src/finance.cpp"],

      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],

      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],

      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ],

      "variables": {
        "openssl_fips": ""
      },

      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "Optimization": 3,
              "InlineFunctionExpansion": 2,
              "EnableEnhancedInstructionSet": 2,
              "FloatingPointModel": 2,
              "FavorSizeOrSpeed": 1,
              "AdditionalOptions": [
                "/openmp",
                "/GL"
              ]
            },
            "VCLinkerTool": {
              "LinkTimeCodeGeneration": 1
            }
          }
        }],

        ["OS=='linux'", {
          "cflags_cc": [
            "-O3",
            "-march=native",
            "-ffast-math",
            "-funroll-loops",
            "-fopenmp",
            "-flto"
          ],
          "ldflags": [
            "-fopenmp",
            "-flto"
          ]
        }],

        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "OTHER_CPLUSPLUSFLAGS": [
              "-O3",
              "-march=native",
              "-ffast-math",
              "-fopenmp",
              "-flto"
            ],
            "OTHER_LDFLAGS": [
              "-fopenmp",
              "-flto"
            ]
          }
        }]
      ]
    }
  ]
}
