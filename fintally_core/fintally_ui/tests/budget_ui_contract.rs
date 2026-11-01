use std::fs;

#[test]
fn budget_page_exposes_crud_actions_without_database_access() {
    let page = fs::read_to_string("ui/pages/app/BudgetsPage.slint").unwrap();
    for required in [
        "refresh-requested",
        "new-requested",
        "form-submit-requested",
        "edit-requested",
        "delete-requested",
        "delete-confirmed",
    ] {
        assert!(page.contains(required), "missing callback: {required}");
    }
    assert!(!page.contains("sqlite"));
    assert!(!page.contains("postgres"));
}

#[test]
fn budget_actions_are_forwarded_through_page_area_and_main_window() {
    let page_area = fs::read_to_string("ui/pages/app/PageArea.slint").unwrap();
    let main = fs::read_to_string("ui/main.slint").unwrap();
    for callback in [
        "budget-new-requested",
        "budget-form-submit-requested",
        "budget-form-cancel-requested",
        "budget-edit-requested",
        "budget-delete-requested",
        "budget-delete-confirmed",
        "budget-delete-cancelled",
    ] {
        assert!(page_area.contains(callback), "PageArea missing {callback}");
        assert!(main.contains(callback), "MainWindow missing {callback}");
    }
}

#[test]
fn responsive_root_keeps_budget_page_in_both_layout_branches() {
    let main = fs::read_to_string("ui/main.slint").unwrap();
    assert_eq!(main.matches("budget-rows <=> root.budget-rows;").count(), 2);
    assert_eq!(main.matches("root.budget-refresh-requested();").count(), 2);
}
