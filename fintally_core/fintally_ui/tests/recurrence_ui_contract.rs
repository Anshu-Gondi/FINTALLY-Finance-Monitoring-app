#[test]
fn recurrence_frequency_uses_a_closed_supported_selector() {
    let page = include_str!("../ui/pages/app/TransactionsPage.slint");

    assert!(page.contains("import { Button, CheckBox, ComboBox, LineEdit, ScrollView }"));
    assert!(page.contains("model: [\"daily\", \"weekly\", \"monthly\"]"));
    assert!(page.contains("current-value <=> root.form-frequency"));
    assert!(page.contains("enabled: !root.busy && root.form-recurring"));
    assert!(page.contains("root.form-frequency"));
}

#[test]
fn recurrence_execution_stays_owned_by_the_kernel_scheduler() {
    let page = include_str!("../ui/pages/app/TransactionsPage.slint");

    assert!(page.contains("kernel scheduler creates future transactions"));
    assert!(page.contains("it never runs recurring jobs"));
}
