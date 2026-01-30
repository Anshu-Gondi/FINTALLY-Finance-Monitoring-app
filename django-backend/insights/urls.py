# insights/urls.py
from django.urls import path
from django.views.decorators.csrf import csrf_exempt
from .schema import schema
from .context import CustomGraphQLView

urlpatterns = [
    path(
        "graphql/",
        csrf_exempt(CustomGraphQLView.as_view(schema=schema)),
        name="graphql",
    ),
]
