import jwt
from fastapi import Header, HTTPException
from typing import Optional

JWT_SECRET = "YOUR_SECRET"  # move to env later


def get_current_user(authorization: Optional[str] = Header(None)):

    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="Authentication required")

    token = authorization.split(" ", 1)[1]

    try:
        payload = jwt.decode(
            token,
            JWT_SECRET,
            algorithms=["HS256"],
        )

    except jwt.ExpiredSignatureError:
        raise HTTPException(status_code=401, detail="Token expired")

    except jwt.InvalidTokenError:
        raise HTTPException(status_code=401, detail="Invalid token")

    user_id = payload.get("userId")

    if not user_id:
        raise HTTPException(status_code=401, detail="Invalid token payload")

    return user_id