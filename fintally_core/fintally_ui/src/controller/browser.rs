use super::budgets::{
    clear_state as clear_budget_state, create_request as create_budget_request,
    parse_form as parse_budget_form, populate_form as populate_budget_form,
    reset_form as reset_budget_form, row_label as budget_row_label,
    update_request as update_budget_request,
};
use super::transactions::{
    create_request, parse_form, populate_form, reset_form, row_label, update_request,
};
use crate::{
    backend::{BackendError, UiBackend},
    MainWindow,
};
use fintally_ipc_protocol::{
    BudgetRecord, DeleteBudgetRequest, StorageMode, TransactionPage, TransactionRecord,
};
use slint::ComponentHandle;
use std::{cell::RefCell, rc::Rc};
use zeroize::Zeroizing;

const TRANSACTION_PAGE_SIZE: u16 = 20;
type Records = Rc<RefCell<Vec<TransactionRecord>>>;
type BudgetRecords = Rc<RefCell<Vec<BudgetRecord>>>;

pub(crate) fn attach_browser(window: &MainWindow) {
    let backend = Rc::new(UiBackend::new());
    let records: Records = Rc::new(RefCell::new(Vec::new()));
    let budget_records: BudgetRecords = Rc::new(RefCell::new(Vec::new()));
    let weak = window.as_weak();

    window.on_login_requested({
        let backend = backend.clone();
        let records = records.clone();
        let budget_records = budget_records.clone();
        let weak = weak.clone();
        move |email, password, local_only_mode| {
            if let Some(ui) = weak.upgrade() {
                records.borrow_mut().clear();
                budget_records.borrow_mut().clear();
                clear_transaction_state(&ui);
                clear_budget_state(&ui);
                ui.set_busy(true);
                ui.set_status_text("Signing in…".into());
                ui.set_password_input("".into());
            }
            let storage_mode = if local_only_mode {
                StorageMode::LocalOnly
            } else {
                StorageMode::CloudSync
            };
            let email = email.to_string();
            let password = Zeroizing::new(password.to_string());
            let backend = backend.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = backend.login(email, password, storage_mode).await;
                if let Some(ui) = weak.upgrade() {
                    match result {
                        Ok(()) => {
                            ui.set_status_text(
                                "Signed in. Select Refresh to load your transaction records."
                                    .into(),
                            );
                            ui.set_authenticated(true);
                            ui.set_transactions_text(
                                "Signed in. Create a transaction or refresh to load your records."
                                    .into(),
                            );
                        }
                        Err(error) => {
                            ui.set_status_text(error.user_message().into());
                            ui.set_authenticated(false);
                            ui.set_transactions_text(login_help().into());
                        }
                    }
                    ui.set_busy(false);
                }
            });
        }
    });

    window.on_signup_requested({
        let backend = backend.clone();
        let weak = weak.clone();
        move |email, password, local_only_mode| {
            if let Some(ui) = weak.upgrade() {
                ui.set_busy(true);
                ui.set_status_text("Creating account…".into());
                ui.set_password_input("".into());
                ui.set_confirm_password_input("".into());
            }
            let storage_mode = if local_only_mode {
                StorageMode::LocalOnly
            } else {
                StorageMode::CloudSync
            };
            let email = email.to_string();
            let password = Zeroizing::new(password.to_string());
            let backend = backend.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = backend.sign_up(email, password, storage_mode).await;
                if let Some(ui) = weak.upgrade() {
                    match result {
                        Ok(()) => {
                            ui.set_current_page("login".into());
                            ui.set_authenticated(false);
                            ui.set_status_text(
                                "Account created. Please sign in with the same storage choice."
                                    .into(),
                            );
                            ui.set_transactions_text(login_help().into());
                        }
                        Err(error) => ui.set_status_text(error.user_message().into()),
                    }
                    ui.set_password_input("".into());
                    ui.set_confirm_password_input("".into());
                    ui.set_busy(false);
                }
            });
        }
    });

    window.on_logout_requested({
        let backend = backend.clone();
        let records = records.clone();
        let budget_records = budget_records.clone();
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() { ui.set_busy(true); ui.set_status_text("Signing out…".into()); }
            let backend = backend.clone();
            let records = records.clone();
            let budget_records = budget_records.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = backend.logout().await;
                records.borrow_mut().clear();
                budget_records.borrow_mut().clear();
                if let Some(ui) = weak.upgrade() {
                    let status = match result {
                        Ok(()) => "Signed out. Sign in to continue.".to_owned(),
                        Err(_) => "Signed out locally. Server revocation could not be confirmed; the server session may remain valid until expiry.".to_owned(),
                    };
                    ui.set_authenticated(false);
                    ui.set_current_page("login".into());
                    ui.set_status_text(status.into());
                    ui.set_transactions_text(login_help().into());
                    clear_transaction_state(&ui);
                    clear_budget_state(&ui);
                    ui.set_password_input("".into());
                    ui.set_confirm_password_input("".into());
                    ui.set_busy(false);
                }
            });
        }
    });

    window.on_refresh_requested({
        let backend = backend.clone();
        let records = records.clone();
        let weak = weak.clone();

        move || {
            schedule_load(backend.clone(), weak.clone(), records.clone(), 1);
        }
    });

    window.on_transaction_page_requested({
        let backend = backend.clone();
        let records = records.clone();
        let weak = weak.clone();
        move |page| {
            if page > 0 {
                schedule_load(backend.clone(), weak.clone(), records.clone(), page as u32);
            }
        }
    });

    window.on_transaction_new_requested({
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() {
                reset_form(&ui);
                ui.set_transaction_editor_open(true);
                ui.set_transaction_delete_confirmation_open(false);
                ui.set_status_text("Enter the transaction details, then create the record.".into());
            }
        }
    });

    window.on_transaction_edit_requested({
        let records = records.clone();
        let weak = weak.clone();
        move |row_index| {
            if row_index < 0 {
                return;
            }
            let record = records.borrow().get(row_index as usize).cloned();
            if let (Some(ui), Some(record)) = (weak.upgrade(), record) {
                populate_form(&ui, &record);
                ui.set_transaction_editor_open(true);
                ui.set_transaction_delete_confirmation_open(false);
                ui.set_status_text(format!("Editing transaction #{}.", record.id).into());
            }
        }
    });

    window.on_transaction_form_cancel_requested({
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() {
                reset_form(&ui);
                ui.set_transaction_editor_open(false);
            }
        }
    });

    window.on_transaction_form_submit_requested({
        let backend = backend.clone();
        let records = records.clone();
        let weak = weak.clone();
        move |name, amount, category, description, timestamp, recurring, frequency| {
            let Some(ui) = weak.upgrade() else { return; };
            let form = match parse_form(
                name.to_string(), amount.to_string(), category.to_string(),
                description.to_string(), timestamp.to_string(), recurring, frequency.to_string(),
            ) {
                Ok(form) => form,
                Err(message) => { ui.set_status_text(message.into()); return; }
            };
            let edit_id = ui.get_transaction_edit_id().to_string();
            let page = ui.get_transaction_page().max(1) as u32;
            if !edit_id.is_empty() && edit_id.parse::<i64>().ok().filter(|id| *id > 0).is_none() {
                ui.set_status_text("The selected transaction identifier is invalid. Reload the list and try again.".into());
                return;
            }
            ui.set_busy(true);
            ui.set_status_text(if edit_id.is_empty() { "Creating transaction…".into() } else { "Saving transaction changes…".into() });
            let backend = backend.clone();
            let records = records.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = if edit_id.is_empty() {
                    backend.create_transaction(create_request(form)).await.map(|_| "Transaction created.".to_owned())
                } else {
                    let id = edit_id.parse::<i64>().unwrap_or_default();
                    backend.update_transaction(update_request(id, form)).await.map(|_| "Transaction updated.".to_owned())
                };
                if let Some(ui) = weak.upgrade() {
                    match result {
                        Ok(message) => {
                            ui.set_status_text(message.into());
                            ui.set_transaction_editor_open(false);
                            reset_form(&ui);
                            ui.set_busy(false);
                            schedule_load(backend, weak.clone(), records, page);
                        }
                        Err(error) => {
                            ui.set_status_text(error.user_message().into());
                            if is_session_error(error) { ui.set_authenticated(false); }
                            ui.set_busy(false);
                        }
                    }
                }
            });
        }
    });

    window.on_transaction_delete_requested({
        let records = records.clone();
        let weak = weak.clone();
        move |row_index| {
            if row_index < 0 {
                return;
            }
            let id = records.borrow().get(row_index as usize).map(|item| item.id);
            if let (Some(ui), Some(id)) = (weak.upgrade(), id) {
                ui.set_transaction_delete_id(id.to_string().into());
                ui.set_transaction_delete_confirmation_open(true);
            }
        }
    });

    window.on_transaction_delete_cancelled({
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() {
                ui.set_transaction_delete_confirmation_open(false);
                ui.set_transaction_delete_id("".into());
            }
        }
    });

    window.on_transaction_delete_confirmed({
        let backend = backend.clone();
        let records = records.clone();
        let weak = weak.clone();
        move |id_text| {
            let Ok(id) = id_text.to_string().parse::<i64>() else {
                if let Some(ui) = weak.upgrade() {
                    ui.set_status_text("Invalid transaction identifier.".into());
                }
                return;
            };
            let page = weak
                .upgrade()
                .map(|ui| ui.get_transaction_page().max(1) as u32)
                .unwrap_or(1);
            if let Some(ui) = weak.upgrade() {
                ui.set_busy(true);
                ui.set_status_text("Deleting transaction…".into());
            }
            let backend = backend.clone();
            let records = records.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = backend.delete_transaction(id).await;
                if let Some(ui) = weak.upgrade() {
                    ui.set_transaction_delete_confirmation_open(false);
                    ui.set_transaction_delete_id("".into());
                    match result {
                        Ok(deleted) if deleted.deleted => {
                            ui.set_status_text(
                                format!("Transaction #{} deleted.", deleted.transaction_id).into(),
                            );
                            ui.set_busy(false);
                            schedule_load(backend, weak.clone(), records, page);
                        }
                        Ok(_) => {
                            ui.set_status_text(
                                "The transaction was not deleted; it may already be gone.".into(),
                            );
                            ui.set_busy(false);
                        }
                        Err(error) => {
                            ui.set_status_text(error.user_message().into());
                            if is_session_error(error) {
                                ui.set_authenticated(false);
                            }
                            ui.set_busy(false);
                        }
                    }
                }
            });
        }
    });

    window.on_budget_refresh_requested({
        let backend = backend.clone();
        let records = budget_records.clone();
        let weak = weak.clone();
        move || schedule_budget_load(backend.clone(), weak.clone(), records.clone())
    });

    window.on_budget_new_requested({
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() {
                reset_budget_form(&ui);
                ui.set_budget_editor_open(true);
                ui.set_budget_delete_confirmation_open(false);
                ui.set_status_text("Enter the budget category, amount and period.".into());
            }
        }
    });

    window.on_budget_edit_requested({
        let records = budget_records.clone();
        let weak = weak.clone();
        move |row_index| {
            if row_index < 0 {
                return;
            }
            let record = records.borrow().get(row_index as usize).cloned();
            if let (Some(ui), Some(record)) = (weak.upgrade(), record) {
                populate_budget_form(&ui, &record);
                ui.set_budget_editor_open(true);
                ui.set_budget_delete_confirmation_open(false);
                ui.set_status_text(format!("Editing budget #{}.", record.id).into());
            }
        }
    });

    window.on_budget_form_cancel_requested({
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() {
                ui.set_budget_editor_open(false);
                reset_budget_form(&ui);
            }
        }
    });

    window.on_budget_form_submit_requested({
        let backend = backend.clone();
        let records = budget_records.clone();
        let weak = weak.clone();
        move |category, amount, start, end| {
            let Some(ui) = weak.upgrade() else {
                return;
            };
            let form = match parse_budget_form(
                category.to_string(),
                amount.to_string(),
                start.to_string(),
                end.to_string(),
            ) {
                Ok(value) => value,
                Err(message) => {
                    ui.set_status_text(message.into());
                    return;
                }
            };
            let edit_id = ui.get_budget_edit_id().to_string();
            let revision = ui.get_budget_edit_revision().to_string();
            let operation = if edit_id.is_empty() {
                if !revision.is_empty() {
                    ui.set_status_text(
                        "The budget edit state is inconsistent. Refresh and try again.".into(),
                    );
                    return;
                }
                None
            } else {
                match (
                    edit_id.parse::<i64>().ok().filter(|value| *value > 0),
                    revision.parse::<i64>().ok().filter(|value| *value > 0),
                ) {
                    (Some(id), Some(revision)) => Some((id, revision)),
                    _ => {
                        ui.set_status_text(
                            "The selected budget revision is invalid. Refresh and try again."
                                .into(),
                        );
                        return;
                    }
                }
            };
            ui.set_busy(true);
            ui.set_status_text(if operation.is_some() {
                "Saving budget changes…".into()
            } else {
                "Creating budget…".into()
            });
            let backend = backend.clone();
            let records = records.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = match operation {
                    Some((id, expected_revision)) => backend
                        .update_budget(update_budget_request(id, expected_revision, form))
                        .await
                        .map(|_| "Budget updated."),
                    None => backend
                        .create_budget(create_budget_request(form))
                        .await
                        .map(|_| "Budget created."),
                };
                if let Some(ui) = weak.upgrade() {
                    match result {
                        Ok(message) => {
                            ui.set_budget_editor_open(false);
                            reset_budget_form(&ui);
                            ui.set_status_text(message.into());
                            ui.set_busy(false);
                            schedule_budget_load(backend, weak.clone(), records);
                        }
                        Err(error) => {
                            ui.set_status_text(error.user_message().into());
                            if is_session_error(error) {
                                ui.set_authenticated(false);
                            }
                            ui.set_busy(false);
                        }
                    }
                }
            });
        }
    });

    window.on_budget_delete_requested({
        let records = budget_records.clone();
        let weak = weak.clone();
        move |row_index| {
            if row_index < 0 {
                return;
            }
            let record = records.borrow().get(row_index as usize).cloned();
            if let (Some(ui), Some(record)) = (weak.upgrade(), record) {
                ui.set_budget_delete_id(record.id.to_string().into());
                ui.set_budget_delete_revision(record.revision.to_string().into());
                ui.set_budget_delete_confirmation_open(true);
            }
        }
    });

    window.on_budget_delete_cancelled({
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() {
                ui.set_budget_delete_confirmation_open(false);
                ui.set_budget_delete_id("".into());
                ui.set_budget_delete_revision("".into());
            }
        }
    });

    window.on_budget_delete_confirmed({
        let backend = backend.clone();
        let records = budget_records.clone();
        let weak = weak.clone();
        move |id_text, revision_text| {
            let id = id_text
                .to_string()
                .parse::<i64>()
                .ok()
                .filter(|value| *value > 0);
            let revision = revision_text
                .to_string()
                .parse::<i64>()
                .ok()
                .filter(|value| *value > 0);
            let (Some(id), Some(expected_revision)) = (id, revision) else {
                if let Some(ui) = weak.upgrade() {
                    ui.set_status_text(
                        "The selected budget revision is invalid. Refresh and try again.".into(),
                    );
                }
                return;
            };
            if let Some(ui) = weak.upgrade() {
                ui.set_busy(true);
                ui.set_status_text("Deleting budget…".into());
            }
            let backend = backend.clone();
            let records = records.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = backend
                    .delete_budget(DeleteBudgetRequest {
                        id,
                        expected_revision,
                    })
                    .await;
                if let Some(ui) = weak.upgrade() {
                    ui.set_budget_delete_confirmation_open(false);
                    ui.set_budget_delete_id("".into());
                    ui.set_budget_delete_revision("".into());
                    match result {
                        Ok(deleted) if deleted.deleted => {
                            ui.set_status_text(
                                format!("Budget #{} deleted.", deleted.budget_id).into(),
                            );
                            ui.set_busy(false);
                            schedule_budget_load(backend, weak.clone(), records);
                        }
                        Ok(_) => {
                            ui.set_status_text("The budget was not deleted.".into());
                            ui.set_busy(false);
                        }
                        Err(error) => {
                            ui.set_status_text(error.user_message().into());
                            if is_session_error(error) {
                                ui.set_authenticated(false);
                            }
                            ui.set_busy(false);
                        }
                    }
                }
            });
        }
    });

    window.on_clear_local_session_requested({
        let backend = backend.clone();
        let records = records.clone();
        let budget_records = budget_records.clone();
        let weak = weak.clone();
        move || {
            if let Some(ui) = weak.upgrade() { ui.set_busy(true); ui.set_status_text("Clearing local session…".into()); }
            let backend = backend.clone();
            let records = records.clone();
            let budget_records = budget_records.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                backend.clear_local_session().await;
                records.borrow_mut().clear();
                budget_records.borrow_mut().clear();
                if let Some(ui) = weak.upgrade() {
                    ui.set_status_text("Local client state cleared. Use Sign out while connected to request server revocation.".into());
                    ui.set_authenticated(false);
                    ui.set_transactions_text(login_help().into());
                    clear_transaction_state(&ui);
                    clear_budget_state(&ui);
                    ui.set_busy(false);
                }
            });
        }
    });
}

fn schedule_budget_load(
    backend: Rc<UiBackend>,
    weak: slint::Weak<MainWindow>,
    records: BudgetRecords,
) {
    if let Some(ui) = weak.upgrade() {
        ui.set_busy(true);
        ui.set_status_text("Loading budgets…".into());
    }
    slint::spawn_local(async move {
        let result = backend.list_budgets().await;
        if let Some(ui) = weak.upgrade() {
            match result {
                Ok(page) => {
                    let labels = page
                        .items
                        .iter()
                        .map(|item| budget_row_label(item).into())
                        .collect::<Vec<slint::SharedString>>();
                    *records.borrow_mut() = page.items;
                    ui.set_budget_rows(slint::ModelRc::new(slint::VecModel::from(labels)));
                    ui.set_status_text("Budgets loaded from the selected storage mode.".into());
                }
                Err(error) => {
                    ui.set_status_text(error.user_message().into());
                    if is_session_error(error) {
                        records.borrow_mut().clear();
                        clear_budget_state(&ui);
                        ui.set_authenticated(false);
                    }
                }
            }
            ui.set_busy(false);
        }
    });
}

fn schedule_load(
    backend: Rc<UiBackend>,
    weak: slint::Weak<MainWindow>,
    records: Records,
    page: u32,
) {
    if let Some(ui) = weak.upgrade() {
        ui.set_busy(true);
        ui.set_status_text(format!("Loading transactions · page {page}…").into());
    }
    slint::spawn_local(async move {
        let result = backend
            .list_transactions(page.max(1), TRANSACTION_PAGE_SIZE)
            .await;
        if let Some(ui) = weak.upgrade() {
            match result {
                Ok(page_data) => {
                    apply_page(&ui, &records, page_data);
                    ui.set_authenticated(true);
                }
                Err(error) => {
                    ui.set_status_text(error.user_message().into());
                    if is_session_error(error) {
                        records.borrow_mut().clear();
                        ui.set_authenticated(false);
                        clear_transaction_state(&ui);
                    }
                }
            }
            ui.set_busy(false);
        }
    });
}

fn apply_page(ui: &MainWindow, records: &Records, page: TransactionPage) {
    *records.borrow_mut() = page.items.clone();
    let labels = page
        .items
        .iter()
        .map(|item| row_label(item).into())
        .collect::<Vec<slint::SharedString>>();
    let count = page.items.len();
    ui.set_transaction_rows(slint::ModelRc::new(slint::VecModel::from(labels)));
    ui.set_transaction_page(page.page.min(i32::MAX as u32) as i32);
    ui.set_transaction_has_more(page.has_more);
    ui.set_transaction_has_items(count > 0);
    ui.set_transactions_text(
        format!("Loaded {count} transaction(s) on page {}.", page.page).into(),
    );
    ui.set_status_text(
        format!(
            "Loaded {count} transaction(s) · page {}{}.",
            page.page,
            if page.has_more {
                " · more records available"
            } else {
                ""
            }
        )
        .into(),
    );
}

fn clear_transaction_state(ui: &MainWindow) {
    ui.set_transaction_rows(slint::ModelRc::new(slint::VecModel::from(Vec::<
        slint::SharedString,
    >::new())));
    ui.set_transaction_page(1);
    ui.set_transaction_has_more(false);
    ui.set_transaction_has_items(false);
    ui.set_transaction_editor_open(false);
    ui.set_transaction_delete_confirmation_open(false);
    ui.set_transaction_delete_id("".into());
    reset_form(ui);
}

fn is_session_error(error: BackendError) -> bool {
    matches!(
        error,
        BackendError::SessionRequired | BackendError::SessionExpired
    )
}

fn login_help() -> String {
    "Sign in to view your transactions. This UI does not store or display the session token."
        .to_owned()
}
