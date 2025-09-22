from django.urls import path
from . import views

urlpatterns = [
    path("daily", views.daily_summary),
    path("weekly", views.weekly_summary),
    path("monthly", views.monthly_summary),
    path("categories", views.category_summary),
    path("lifetime", views.lifetime_analysis),
    path("max", views.max_transaction),
    path("min", views.min_transaction),
    path("trends", views.trend_summary),
]
