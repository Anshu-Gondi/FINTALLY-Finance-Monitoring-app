import _native

# Re-export the Rust class
PyLLM = _native.llm.PyLLM

# Factory function
def create_llm(*args, **kwargs):
    return _native.llm.create_llm(*args, **kwargs)
