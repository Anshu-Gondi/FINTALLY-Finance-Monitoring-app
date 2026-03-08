from fastapi import Request


def get_request_context(request: Request):
    return {
        "request": request
    }