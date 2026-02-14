{
  "targets": [
    {
      "target_name": "finance",
      "sources": [ "src/finance.cpp" ],

      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],

      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],

      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],

      "cflags_cc": [
        "-O3",
        "-march=native",
        "-msse4.2",
        "-ffast-math",
        "-funroll-loops"
      ],

      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "Optimization": 3,
              "EnableEnhancedInstructionSet": 2
            }
          }
        }],
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "OTHER_CPLUSPLUSFLAGS": [
              "-O3",
              "-march=native",
              "-ffast-math"
            ]
          }
        }]
      ]
    }
  ]
}
