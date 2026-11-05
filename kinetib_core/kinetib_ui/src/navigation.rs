//! Typed route state for the FinTally multi-page UI.
//!
//! This module contains presentation/navigation state only. It does not hold a
//! bearer token, user ID, database connection, or provider secret.

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Page {
    Login,
    Signup,
    GoogleSignIn,
    Dashboard,
    Transactions,
    Budgets,
    Emi,
    Analytics,
    Chat,
    Vision,
    Voice,
    Settings,
    Feedback,
}

impl Page {
    pub const fn id(self) -> &'static str {
        match self {
            Self::Login => "login",
            Self::Signup => "signup",
            Self::GoogleSignIn => "google",
            Self::Dashboard => "dashboard",
            Self::Transactions => "transactions",
            Self::Budgets => "budgets",
            Self::Emi => "emi",
            Self::Analytics => "analytics",
            Self::Chat => "chat",
            Self::Vision => "vision",
            Self::Voice => "voice",
            Self::Settings => "settings",
            Self::Feedback => "feedback",
        }
    }

    pub const fn path(self) -> &'static str {
        match self {
            Self::Login => "/auth/login",
            Self::Signup => "/auth/signup",
            Self::GoogleSignIn => "/auth/google",
            Self::Dashboard => "/app/dashboard",
            Self::Transactions => "/app/transactions",
            Self::Budgets => "/app/budgets",
            Self::Emi => "/app/emi",
            Self::Analytics => "/app/analytics",
            Self::Chat => "/app/chat",
            Self::Vision => "/app/vision",
            Self::Voice => "/app/voice",
            Self::Settings => "/app/settings",
            Self::Feedback => "/app/feedback",
        }
    }

    pub fn from_id(value: &str) -> Option<Self> {
        Some(match value {
            "login" => Self::Login,
            "signup" => Self::Signup,
            "google" => Self::GoogleSignIn,
            "dashboard" => Self::Dashboard,
            "transactions" => Self::Transactions,
            "budgets" => Self::Budgets,
            "emi" => Self::Emi,
            "analytics" => Self::Analytics,
            "chat" => Self::Chat,
            "vision" => Self::Vision,
            "voice" => Self::Voice,
            "settings" => Self::Settings,
            "feedback" => Self::Feedback,
            _ => return None,
        })
    }

    pub const fn is_public(self) -> bool {
        matches!(self, Self::Login | Self::Signup | Self::GoogleSignIn)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AuthState {
    Anonymous,
    Authenticating,
    Authenticated,
    Expired,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NavigationResult {
    Navigated(Page),
    RedirectedToLogin,
    UnknownPage,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavigationState {
    page: Page,
    auth: AuthState,
}

impl Default for NavigationState {
    fn default() -> Self {
        Self {
            page: Page::Login,
            auth: AuthState::Anonymous,
        }
    }
}

impl NavigationState {
    pub const fn page(self) -> Page {
        self.page
    }
    pub const fn auth_state(self) -> AuthState {
        self.auth
    }

    pub fn navigate_id(&mut self, requested: &str) -> NavigationResult {
        let Some(page) = Page::from_id(requested) else {
            return NavigationResult::UnknownPage;
        };
        self.navigate(page)
    }

    pub fn navigate(&mut self, requested: Page) -> NavigationResult {
        if !requested.is_public() && self.auth != AuthState::Authenticated {
            self.page = Page::Login;
            return NavigationResult::RedirectedToLogin;
        }
        self.page = requested;
        NavigationResult::Navigated(requested)
    }

    pub fn begin_authentication(&mut self) {
        self.auth = AuthState::Authenticating;
    }

    pub fn authentication_succeeded(&mut self) {
        self.auth = AuthState::Authenticated;
        self.page = Page::Dashboard;
    }

    pub fn session_expired(&mut self) {
        self.auth = AuthState::Expired;
        self.page = Page::Login;
    }

    pub fn logged_out(&mut self) {
        self.auth = AuthState::Anonymous;
        self.page = Page::Login;
    }
}

#[cfg(test)]
mod tests {
    use super::{AuthState, NavigationResult, NavigationState, Page};

    #[test]
    fn anonymous_users_cannot_navigate_to_protected_pages() {
        let mut router = NavigationState::default();
        assert_eq!(
            router.navigate(Page::Transactions),
            NavigationResult::RedirectedToLogin
        );
        assert_eq!(router.page(), Page::Login);
    }

    #[test]
    fn login_signup_and_google_are_public_routes() {
        let mut router = NavigationState::default();
        assert_eq!(
            router.navigate(Page::Signup),
            NavigationResult::Navigated(Page::Signup)
        );
        assert_eq!(
            router.navigate(Page::GoogleSignIn),
            NavigationResult::Navigated(Page::GoogleSignIn)
        );
    }

    #[test]
    fn feedback_route_is_protected_and_resolves_to_its_page() {
        let mut router = NavigationState::default();
        assert_eq!(
            router.navigate_id("feedback"),
            NavigationResult::RedirectedToLogin
        );
        router.authentication_succeeded();
        assert_eq!(
            router.navigate_id("feedback"),
            NavigationResult::Navigated(Page::Feedback)
        );
        assert_eq!(Page::Feedback.path(), "/app/feedback");
    }

    #[test]
    fn unknown_route_is_rejected() {
        let mut router = NavigationState::default();
        assert_eq!(
            router.navigate_id("delete-everything"),
            NavigationResult::UnknownPage
        );
    }

    #[test]
    fn authentication_success_lands_on_dashboard() {
        let mut router = NavigationState::default();
        router.begin_authentication();
        router.authentication_succeeded();
        assert_eq!(router.auth_state(), AuthState::Authenticated);
        assert_eq!(router.page(), Page::Dashboard);
    }

    #[test]
    fn session_expiry_and_logout_return_to_login() {
        let mut router = NavigationState::default();
        router.authentication_succeeded();
        router.session_expired();
        assert_eq!(router.auth_state(), AuthState::Expired);
        assert_eq!(router.page(), Page::Login);
        router.logged_out();
        assert_eq!(router.auth_state(), AuthState::Anonymous);
    }
}
