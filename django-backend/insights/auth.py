import jwt
from django.conf import settings
from strawberry.exceptions import GraphQLError
from strawberry.types import Info

def get_current_user(info: Info):
    request = info.context["request"]
    auth_header = request.headers.get("Authorization")

    if not auth_header or not auth_header.startswith("Bearer "):
        raise GraphQLError("Authentication required")

    token = auth_header.split(" ", 1)[1]

    try:
        payload = jwt.decode(
            token,
            settings.JWT_SECRET,
            algorithms=["HS256"],
        )
    except jwt.ExpiredSignatureError:
        raise GraphQLError("Token expired")
    except jwt.InvalidTokenError:
        raise GraphQLError("Invalid token")

    # This is exactly what Node puts in the token
    user_id = payload.get("userId")
    if not user_id:
        raise GraphQLError("Invalid token payload")

    return user_id
