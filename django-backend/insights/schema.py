import strawberry
from .resolvers import Query

schema = strawberry.Schema(query=Query)
