//! Native budget controller wiring and bounded record loading.

use super::super::budgets::{
    clear_state as clear_budget_state, create_request as create_budget_request,
    parse_form as parse_budget_form, populate_form as populate_budget_form,
    reset_form as reset_budget_form, row_label as budget_row_label,
    update_request as update_budget_request,
};
use crate::{
    backend::{BackendError, UiBackend},
    MainWindow,
};
use kinetib_ipc_protocol::{BudgetRecord, DeleteBudgetRequest};
use slint::ComponentHandle;
use std::sync::{Arc, Mutex};
use tokio::runtime::Handle;

pub(super) type BudgetRecords = Arc<Mutex<Vec<BudgetRecord>>>;

pub(super) fn attach(
    window: &MainWindow,
    backend: Arc<UiBackend>,
    runtime: Handle,
    budget_records: BudgetRecords,
) {
    let weak = window.as_weak();

    // -------------------------------------------------------------------------
    // BUDGET REFRESH
    // -------------------------------------------------------------------------

    window.on_budget_refresh_requested({
        let backend = Arc::clone(&backend);
        let records = Arc::clone(&budget_records);
        let runtime = runtime.clone();
        let weak = weak.clone();

        move || {
            schedule_budget_load(
                Arc::clone(&backend),
                weak.clone(),
                runtime.clone(),
                Arc::clone(&records),
            );
        }
    });

    // -------------------------------------------------------------------------
    // BUDGET CREATE FORM
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // BUDGET EDIT FORM
    // -------------------------------------------------------------------------

    window.on_budget_edit_requested({
        let records = Arc::clone(&budget_records);
        let weak = weak.clone();

        move |row_index| {
            if row_index < 0 {
                return;
            }

            let record = records
                .lock()
                .ok()
                .and_then(|items| items.get(row_index as usize).cloned());

            if let (Some(ui), Some(record)) = (weak.upgrade(), record) {
                populate_budget_form(&ui, &record);

                ui.set_budget_editor_open(true);
                ui.set_budget_delete_confirmation_open(false);

                ui.set_status_text(format!("Editing budget #{}.", record.id).into());
            }
        }
    });

    // -------------------------------------------------------------------------
    // BUDGET FORM CANCEL
    // -------------------------------------------------------------------------

    window.on_budget_form_cancel_requested({
        let weak = weak.clone();

        move || {
            if let Some(ui) = weak.upgrade() {
                ui.set_budget_editor_open(false);
                reset_budget_form(&ui);
            }
        }
    });

    // -------------------------------------------------------------------------
    // BUDGET CREATE / UPDATE
    //
    // Important:
    // task_runtime is used to spawn the task.
    // reload_runtime is moved into the task for the later UI refresh.
    // They are separate Handle clones, avoiding E0505.
    // -------------------------------------------------------------------------

    window.on_budget_form_submit_requested({
        let backend = Arc::clone(&backend);
        let records = Arc::clone(&budget_records);
        let runtime = runtime.clone();
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
                let id = edit_id.parse::<i64>().ok().filter(|value| *value > 0);

                let revision = revision.parse::<i64>().ok().filter(|value| *value > 0);

                match (id, revision) {
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

            let backend = Arc::clone(&backend);
            let records = Arc::clone(&records);
            let weak = weak.clone();

            // FIX: clone both handles before moving the async task.
            let task_runtime = runtime.clone();
            let reload_runtime = runtime.clone();

            task_runtime.spawn(async move {
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

                let reload_backend = Arc::clone(&backend);
                let reload_records = Arc::clone(&records);

                let _ = slint::invoke_from_event_loop(move || {
                    if let Some(ui) = weak.upgrade() {
                        match result {
                            Ok(message) => {
                                ui.set_budget_editor_open(false);
                                reset_budget_form(&ui);

                                ui.set_status_text(message.into());
                                ui.set_busy(false);

                                schedule_budget_load(
                                    reload_backend,
                                    weak.clone(),
                                    reload_runtime,
                                    reload_records,
                                );
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
            });
        }
    });

    // -------------------------------------------------------------------------
    // BUDGET DELETE REQUEST
    // -------------------------------------------------------------------------

    window.on_budget_delete_requested({
        let records = Arc::clone(&budget_records);
        let weak = weak.clone();

        move |row_index| {
            if row_index < 0 {
                return;
            }

            let record = records
                .lock()
                .ok()
                .and_then(|items| items.get(row_index as usize).cloned());

            if let (Some(ui), Some(record)) = (weak.upgrade(), record) {
                ui.set_budget_delete_id(record.id.to_string().into());
                ui.set_budget_delete_revision(record.revision.to_string().into());
                ui.set_budget_delete_confirmation_open(true);
            }
        }
    });

    // -------------------------------------------------------------------------
    // BUDGET DELETE CANCEL
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // BUDGET DELETE CONFIRM
    //
    // Important:
    // The reload runtime is cloned before spawn, not inside the async block.
    // -------------------------------------------------------------------------

    window.on_budget_delete_confirmed({
        let backend = Arc::clone(&backend);
        let records = Arc::clone(&budget_records);
        let runtime = runtime.clone();
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

            let backend = Arc::clone(&backend);
            let records = Arc::clone(&records);
            let weak = weak.clone();

            // FIX: separate handles for spawn and the subsequent reload.
            let task_runtime = runtime.clone();
            let reload_runtime = runtime.clone();

            task_runtime.spawn(async move {
                let result = backend
                    .delete_budget(DeleteBudgetRequest {
                        id,
                        expected_revision,
                    })
                    .await;

                let reload_backend = Arc::clone(&backend);
                let reload_records = Arc::clone(&records);

                let _ = slint::invoke_from_event_loop(move || {
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

                                schedule_budget_load(
                                    reload_backend,
                                    weak.clone(),
                                    reload_runtime,
                                    reload_records,
                                );
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
            });
        }
    });
}

fn schedule_budget_load(
    backend: Arc<UiBackend>,
    weak: slint::Weak<MainWindow>,
    runtime: Handle,
    records: BudgetRecords,
) {
    if let Some(ui) = weak.upgrade() {
        ui.set_busy(true);
        ui.set_status_text("Loading budgets…".into());
    }

    runtime.spawn(async move {
        let result = backend.list_budgets().await;

        let _ = slint::invoke_from_event_loop(move || {
            if let Some(ui) = weak.upgrade() {
                match result {
                    Ok(page) => {
                        let labels = page
                            .items
                            .iter()
                            .map(|item| budget_row_label(item).into())
                            .collect::<Vec<slint::SharedString>>();

                        if let Ok(mut current) = records.lock() {
                            *current = page.items;
                        }

                        ui.set_budget_rows(slint::ModelRc::new(slint::VecModel::from(labels)));

                        ui.set_status_text("Budgets loaded from the selected storage mode.".into());
                    }
                    Err(error) => {
                        ui.set_status_text(error.user_message().into());

                        if is_session_error(error) {
                            if let Ok(mut current) = records.lock() {
                                current.clear();
                            }

                            clear_budget_state(&ui);
                            ui.set_authenticated(false);
                        }
                    }
                }

                ui.set_busy(false);
            }
        });
    });
}

fn is_session_error(error: BackendError) -> bool {
    matches!(
        error,
        BackendError::SessionRequired | BackendError::SessionExpired
    )
}
