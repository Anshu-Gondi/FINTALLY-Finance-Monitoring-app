from strawberry.django.views import GraphQLView

class CustomGraphQLView(GraphQLView):
    def get_context(self, request, response):
        return {
            "request": request,
            "response": response,
        }
