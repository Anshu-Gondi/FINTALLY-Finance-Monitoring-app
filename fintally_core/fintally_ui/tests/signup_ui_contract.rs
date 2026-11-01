//! Static contracts for auth-page wiring. These complement, but do not replace,
//! integration tests against a running FinTally kernel and real storage.

const ROOT_UI: &str = include_str!("../ui/main.slint");
const SIGNUP_PAGE: &str = include_str!("../ui/pages/auth/SignupPage.slint");
const NATIVE_CONTROLLER: &str = include_str!("../src/controller/native.rs");
const BROWSER_CONTROLLER: &str = include_str!("../src/controller/browser.rs");
const NATIVE_BACKEND: &str = include_str!("../src/backend/native.rs");
const BROWSER_BACKEND: &str = include_str!("../src/backend/browser.rs");

#[test]
fn signup_page_has_no_unpersisted_display_name_field() {
    assert!(SIGNUP_PAGE.contains("email-input"));
    assert!(SIGNUP_PAGE.contains("password-input"));
    assert!(SIGNUP_PAGE.contains("confirm-password-input"));
    assert!(!SIGNUP_PAGE.contains("name-input"));
}

#[test]
fn signup_callback_is_connected_to_both_platform_controllers() {
    assert!(ROOT_UI.contains("callback signup-requested(string, string, bool);"));
    assert!(SIGNUP_PAGE.contains("callback signup-requested(string, string, string, bool);"));
    assert!(NATIVE_CONTROLLER.contains("window.on_signup_requested"));
    assert!(NATIVE_CONTROLLER.contains("backend.sign_up(email, password, storage_mode)"));
    assert!(BROWSER_CONTROLLER.contains("window.on_signup_requested"));
    assert!(BROWSER_CONTROLLER.contains("backend.sign_up(email, password, storage_mode)"));
}

#[test]

fn selected_storage_mode_is_forwarded_to_signup_and_signin() {
    assert!(ROOT_UI.contains("local-only-mode: true"));

    // Normalize whitespace so rustfmt line wrapping does not break

    // the source contract.

    let native_backend_compact = NATIVE_BACKEND
        .chars()
        .filter(|c| !c.is_whitespace())
        .collect::<String>();

    let browser_backend_compact = BROWSER_BACKEND
        .chars()
        .filter(|c| !c.is_whitespace())
        .collect::<String>();

    assert!(native_backend_compact.contains("client.sign_up(email,password,storage_mode).await"));

    assert!(native_backend_compact.contains("client.sign_in(email,password,storage_mode).await"));

    assert!(browser_backend_compact
        .contains("client.sign_up(&email,password.as_str(),storage_mode).await"));

    assert!(browser_backend_compact
        .contains("client.sign_in(&email,password.as_str(),storage_mode).await"));
}

#[test]
fn account_creation_routes_back_to_signin_without_retaining_a_session() {
    assert!(NATIVE_BACKEND.contains("let _ = client.logout().await;"));
    assert!(BROWSER_BACKEND.contains("let _ = client.logout().await;"));
    assert!(NATIVE_CONTROLLER.contains("ui.set_current_page(\"login\".into());"));
    assert!(BROWSER_CONTROLLER.contains("ui.set_current_page(\"login\".into());"));
    assert!(
        NATIVE_CONTROLLER.contains("Account created. Please sign in with the same storage choice.")
    );
    assert!(BROWSER_CONTROLLER
        .contains("Account created. Please sign in with the same storage choice."));
}
