use kinetib_ui::navigation::{AuthState, NavigationResult, NavigationState, Page};

#[test]
fn every_named_route_round_trips() {
    let pages = [
        Page::Login,
        Page::Signup,
        Page::GoogleSignIn,
        Page::Dashboard,
        Page::Transactions,
        Page::Budgets,
        Page::Emi,
        Page::Analytics,
        Page::Chat,
        Page::Vision,
        Page::Voice,
        Page::Settings,
    ];
    for page in pages {
        assert_eq!(Page::from_id(page.id()), Some(page));
        assert!(page.path().starts_with('/'));
    }
}

#[test]
fn protected_route_requires_authenticated_state() {
    let mut nav = NavigationState::default();
    assert_eq!(
        nav.navigate_id("transactions"),
        NavigationResult::RedirectedToLogin
    );
    assert_eq!(nav.auth_state(), AuthState::Anonymous);
}
