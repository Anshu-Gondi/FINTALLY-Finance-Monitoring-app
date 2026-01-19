import { useEffect, useState, useCallback } from "react";
import "bulma/css/bulma.min.css";
import "./App.css";
import Flatpickr from "react-flatpickr";
import "flatpickr/dist/themes/dark.css";
import Modal from "react-modal";
import Navbar from "./Shared Components/Navbar/Navbar";
import Footer from "./Shared Components/Footer/Footer";

Modal.setAppElement("#root");

function App() {
  const [transactions, setTransactions] = useState([]);
  const [name, setName] = useState("");
  const [description, setDescription] = useState("");
  const [datetime, setDatetime] = useState("");
  const [price, setPrice] = useState("");
  const [category, setCategory] = useState("");
  const [isRecurring, setIsRecurring] = useState(false);
  const [recurringFrequency, setRecurringFrequency] = useState("");
  const [receipt, setReceipt] = useState(null);
  const [editingId, setEditingId] = useState(null);

  const [searchQuery, setSearchQuery] = useState("");
  const [page, setPage] = useState(1);
  const [hasMore, setHasMore] = useState(true);
  const [balance, setBalance] = useState(0);

  const [isEMI, setIsEMI] = useState(false);
  const [emiPrincipal, setEmiPrincipal] = useState("");
  const [emiRate, setEmiRate] = useState("");
  const [emiTenure, setEmiTenure] = useState("");
  const [emiAmount, setEmiAmount] = useState(0);
  const [emiAffordable, setEmiAffordable] = useState(true);

  // 🟢 Budget state
  const [budgets, setBudgets] = useState([]);
  const [budgetAmount, setBudgetAmount] = useState("");
  const [budgetCategory, setBudgetCategory] = useState("Overall");
  const [budgetStartDate, setBudgetStartDate] = useState(null);
  const [budgetEndDate, setBudgetEndDate] = useState(null);

  const overallBudget = budgets.find(b => b.category === "Overall");
  const monthlyBudget = overallBudget?.amount || 0;

  const API_URL = import.meta.env.VITE_API_URL;

  const fetchBudgets = useCallback(async () => {
    const res = await fetch(`${API_URL}/budget`, {
      headers: {
        Authorization: "Bearer " + localStorage.getItem("token"),
      },
    });
    const json = await res.json();
    if (json.success) setBudgets(json.data);
  }, [API_URL]);

  useEffect(() => {
    fetchBudgets();
  }, [fetchBudgets]);


  const saveBudget = async () => {
    if (!budgetAmount) {
      alert("Please enter budget amount");
      return;
    }

    const res = await fetch(`${API_URL}/budget`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: "Bearer " + localStorage.getItem("token"),
      },
      body: JSON.stringify({
        amount: Number(budgetAmount),
        category: budgetCategory,
        startDate: budgetStartDate,
        endDate: budgetEndDate,
        isRecurring: true,
      }),
    });

    const json = await res.json();

    if (json.success) {
      alert("Budget saved");
      setBudgetAmount("");
      setBudgetStartDate(null);
      setBudgetEndDate(null);
      fetchBudgets();
    }
  };


  // 🔹 Active EMI transactions
  const activeEMIs = transactions.filter(
    (t) =>
      t.isRecurring &&
      t.emiMeta &&
      t.recurringFrequency === "Monthly" &&
      t.emiMeta.tenureMonths > 0,
  );

  // 🔹 Monthly EMI outflow
  const totalMonthlyEMI = activeEMIs.reduce(
    (sum, t) => sum + Math.abs(t.price),
    0,
  );

  // 🔹 EMI budget usage %
  const emiBudgetPercent =
    monthlyBudget > 0
      ? Math.min((totalMonthlyEMI / monthlyBudget) * 100, 100)
      : 0;

  const fetchTransactions = useCallback(
    async (pageNum) => {
      const res = await fetch(`${API_URL}/transaction?page=${pageNum}`, {
        headers: { Authorization: "Bearer " + localStorage.getItem("token") },
      });
      const json = await res.json();
      if (json.data.length === 0) {
        setHasMore(false);
      } else {
        setTransactions((prev) => [...prev, ...json.data]);
      }
    },
    [API_URL],
  );

  const fetchAndReplaceTransactions = async () => {
    const res = await fetch(`${API_URL}/transaction?page=1`, {
      headers: { Authorization: "Bearer " + localStorage.getItem("token") },
    });
    const json = await res.json();
    setTransactions(json.data);
    setPage(1);
    setHasMore(true);
  };

  useEffect(() => {
    fetchTransactions(page);
  }, [page, fetchTransactions]);

  useEffect(() => {
    const handleScroll = () => {
      if (
        window.innerHeight + window.scrollY >=
        document.body.offsetHeight - 50 &&
        hasMore
      ) {
        setPage((prev) => prev + 1);
      }
    };
    window.addEventListener("scroll", handleScroll);
    return () => window.removeEventListener("scroll", handleScroll);
  }, [hasMore]);

  useEffect(() => {
    const uniqueTx = Array.from(
      new Map(transactions.map((t) => [t._id, t])).values(),
    );
    const total = uniqueTx.reduce((acc, t) => acc + t.price, 0);
    setBalance(total);
  }, [transactions]);

  const addNewTransaction = async (ev) => {
    ev.preventDefault();

    const formData = new FormData();
    formData.append("name", name);
    const finalPrice = isEMI ? -emiAmount : price;
    formData.append("price", finalPrice);
    formData.append("description", description);
    formData.append("datetime", datetime);
    formData.append("category", category);
    formData.append("isRecurring", isRecurring);
    if (isRecurring) formData.append("recurringFrequency", recurringFrequency);
    if (isEMI && recurringFrequency !== "Monthly") {
      alert("EMI must be monthly");
      return;
    }
    if (isEMI && !isRecurring) {
      alert("EMI must be recurring");
      return;
    }
    if (isEMI) {
      const res = await fetch(`${API_URL}/emi/create`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: "Bearer " + localStorage.getItem("token"),
        },
        body: JSON.stringify({
          principal: emiPrincipal,
          annualRate: emiRate,
          months: emiTenure,
          category,
          name
        }),
      });

      const json = await res.json();
      if (!json.success) {
        alert(json.message || "EMI creation failed");
        return;
      }

      resetForm();
      await fetchAndReplaceTransactions();
      return;
    }

    if (isEMI) {
      if (!emiAffordable) {
        alert("EMI not affordable under current budget");
        return;
      }

      formData.append(
        "emiMeta",
        JSON.stringify({
          principal: emiPrincipal,
          annualRate: emiRate,
          tenureMonths: emiTenure,
          originalTenure: emiTenure,
        }),
      );
    }
    if (receipt) formData.append("receipt", receipt);

    const res = await fetch(`${API_URL}/transaction`, {
      method: "POST",
      headers: {
        Authorization: "Bearer " + localStorage.getItem("token"),
      },
      body: formData,
    });

    if (res.ok) {
      await res.json();
      resetForm();
      await fetchAndReplaceTransactions(); // Clear and refetch page 1 cleanly
    }
  };

  const updateTransaction = async (ev) => {
    ev.preventDefault();

    const updatedData = {
      name,
      price,
      description,
      datetime,
      category,
      isRecurring,
      recurringFrequency,
    };

    const res = await fetch(`${API_URL}/transaction/${editingId}`, {
      method: "PUT",
      headers: {
        "Content-Type": "application/json",
        Authorization: "Bearer " + localStorage.getItem("token"),
      },
      body: JSON.stringify(updatedData),
    });

    if (res.ok) {
      const updated = await res.json();
      setTransactions((prev) =>
        prev.map((tx) => (tx._id === editingId ? updated.data : tx)),
      );
      resetForm();
    }
  };

  const handleEdit = (transaction) => {
    setEditingId(transaction._id);
    setName(transaction.name);
    setDescription(transaction.description);
    setDatetime(transaction.datetime);
    setPrice(transaction.price);
    setCategory(transaction.category);
    setIsRecurring(transaction.isRecurring);
    setRecurringFrequency(transaction.recurringFrequency || "");

    if (transaction.emiMeta) {
      setIsEMI(true);
      setEmiPrincipal(transaction.emiMeta.principal);
      setEmiRate(transaction.emiMeta.annualRate);
      setEmiTenure(transaction.emiMeta.tenureMonths);
    }
  };

  const handleDelete = async (id) => {
    const confirm = window.confirm(
      "Are you sure you want to delete this transaction?",
    );
    if (!confirm) return;

    const res = await fetch(`${API_URL}/transaction/${id}`, {
      method: "DELETE",
      headers: {
        Authorization: "Bearer " + localStorage.getItem("token"),
      },
    });

    if (res.ok) {
      setTransactions((prev) => prev.filter((t) => t._id !== id));
    }
  };

  const resetForm = () => {
    setEditingId(null);
    setName("");
    setDescription("");
    setDatetime("");
    setPrice("");
    setCategory("");
    setIsRecurring(false);
    setRecurringFrequency("");
    setReceipt(null);
    setIsEMI(false);
    setEmiPrincipal("");
    setEmiRate("");
    setEmiTenure("");
    setEmiAmount(0);
  };

  const calculateEMI = useCallback(() => {
    if (!emiPrincipal || !emiRate || !emiTenure) return 0;

    const P = Number(emiPrincipal);
    const r = Number(emiRate) / 12 / 100;
    const n = Number(emiTenure);

    const emi = (P * r * Math.pow(1 + r, n)) / (Math.pow(1 + r, n) - 1);

    return Number.isFinite(emi) ? emi : 0;
  }, [emiPrincipal, emiRate, emiTenure]);

  useEffect(() => {
    const emi = calculateEMI();
    setEmiAmount(emi);

    if (!emi || !isEMI) return;

    fetch(`${API_URL}/emi/check`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: "Bearer " + localStorage.getItem("token"),
      },
      body: JSON.stringify({
        emiAmount: emi,
        category
      }),
    })
      .then(res => res.json())
      .then(json => {
        setEmiAffordable(json.data?.budgetImpact?.affordable ?? true);
      });
  }, [calculateEMI, isEMI, emiPrincipal, emiRate, emiTenure, category, API_URL]);

  const filteredTransactions = transactions.filter(
    (t) =>
      t.name?.toLowerCase().includes(searchQuery) ||
      t.description?.toLowerCase().includes(searchQuery) ||
      t.category?.toLowerCase().includes(searchQuery),
  );

  const uniqueTransactions = Array.from(
    new Map(filteredTransactions.map((t) => [t._id, t])).values(),
  );

  return (
    <>
      <Navbar />
      <div className="appContainer">
        <h1 className="appTitle">💸 FinTally</h1>
        <div className="neonBox mt-5">
          <h3 className="title is-5 budgetTitle">
            📊 Budget Control Center
          </h3>
          <p className="budgetSubtitle">
            Define limits to keep your spending & EMIs in control
          </p>

          <div className="neonBox budgetCreateBox">
            <h4 className="title is-6 is-center">➕ Set / Update Budget</h4>
            <div className="field">
              <label className="label">Category</label>
              <div className="select is-fullwidth neonSelectWrapper">
                <select
                  value={budgetCategory}
                  onChange={(e) => setBudgetCategory(e.target.value)}
                >
                  <option value="Overall">Overall</option>
                  <option value="Food">Food</option>
                  <option value="Shopping">Shopping</option>
                  <option value="Travel">Travel</option>
                  <option value="Bills">Bills</option>
                  <option value="Entertainment">Entertainment</option>
                </select>
              </div>
            </div>

            <div className="field">
              <label className="label">Amount (₹)</label>
              <input
                className="input neonInput"
                type="number"
                value={budgetAmount}
                onChange={(e) => setBudgetAmount(e.target.value)}
              />
            </div>

            <div className="field">
              <label className="label">Start Date</label>
              <Flatpickr
                value={budgetStartDate}
                onChange={([d]) => setBudgetStartDate(d?.toISOString())}
                className="input neonInput"
              />
            </div>

            <div className="field">
              <label className="label">End Date (optional)</label>
              <Flatpickr
                value={budgetEndDate}
                onChange={([d]) => setBudgetEndDate(d?.toISOString())}
                className="input neonInput"
              />
            </div>

            <button className="neonButton" onClick={saveBudget}>
              Save Budget
            </button>
          </div>

          <div className="neonBox mt-4">
            <h4 className="title is-6">📋 Active Budgets</h4>

            <div className="budgetGrid">
              {budgets.map((b) => {
                const spent =
                  Math.abs(
                    transactions
                      .filter(t => t.category === b.category || b.category === "Overall")
                      .reduce((s, t) => s + (t.price < 0 ? t.price : 0), 0)
                  );

                const percent = Math.min((spent / b.amount) * 100, 100);

                return (
                  <div key={b._id} className="budgetCard">
                    <div className="budgetCardHeader">
                      <span className="budgetCategory">{b.category}</span>
                      <span className="budgetAmount">₹{b.amount}</span>
                    </div>

                    <progress
                      className={`progress ${percent > 80 ? "is-danger" : "is-info"}`}
                      value={percent}
                      max="100"
                    />

                    <p className="budgetMeta">
                      Used: ₹{spent.toFixed(0)} ({percent.toFixed(1)}%)
                    </p>

                    <p className="budgetDates">
                      {new Date(b.startDate).toLocaleDateString()} →{" "}
                      {b.endDate
                        ? new Date(b.endDate).toLocaleDateString()
                        : "Ongoing"}
                    </p>
                  </div>
                );
              })}
            </div>
          </div>
        </div>



        <h2 className="appBalance">Balance: ₹{balance.toFixed(2)}</h2>

        {balance < -monthlyBudget && (
          <p className="budgetAlert">
            🚨 Budget exceeded by ₹
            {Math.abs(balance + monthlyBudget).toFixed(2)}!
          </p>
        )}

        {activeEMIs.length > 0 && (
          <div className="neonBox mt-4">
            <h3 className="title is-5">💳 EMI Load Monitor</h3>

            <p>
              📌 Active EMIs: <strong>{activeEMIs.length}</strong>
            </p>
            <p>
              💸 Total Monthly EMI:{" "}
              <strong>₹{totalMonthlyEMI.toFixed(2)}</strong>
            </p>
            <p>
              📊 EMI Budget Usage:{" "}
              <strong>{emiBudgetPercent.toFixed(1)}%</strong>
            </p>

            <progress
              className={`progress ${emiBudgetPercent > 80 ? "is-danger" : "is-primary"
                }`}
              value={emiBudgetPercent}
              max="100"
            >
              {emiBudgetPercent}%
            </progress>

            {emiBudgetPercent > 80 && (
              <p className="has-text-danger mt-2">⚠ EMI burden is high</p>
            )}
          </div>
        )}

        <div className="formWrapper">
          <form
            onSubmit={editingId ? updateTransaction : addNewTransaction}
            className="transactionForm"
          >
            <div className="field">
              <label className="label">Name</label>
              <input
                className="input neonInput"
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="Enter transaction name"
                required
              />
            </div>

            <div className="field">
              <label className="label">Description</label>
              <input
                className="input neonInput"
                value={description}
                onChange={(e) => setDescription(e.target.value)}
                placeholder="Enter description"
              />
            </div>

            <div className="field">
              <label className="label">Amount (₹)</label>
              <input
                className="input neonInput"
                type="number"
                value={price}
                disabled={isEMI}
                onChange={(e) => setPrice(Number(e.target.value))}
                placeholder={
                  isEMI
                    ? "Calculated from EMI"
                    : "Amount (negative for expense)"
                }
                required={!isEMI}
              />
            </div>

            <div className="field">
              <label className="label">Date & Time</label>
              <Flatpickr
                data-enable-time
                value={datetime ? new Date(datetime) : null}
                onChange={([date]) => setDatetime(date.toISOString())}
                options={{
                  enableTime: true,
                  dateFormat: "Y-m-d H:i",
                  altInput: true,
                  altFormat: "F j, Y h:i K",
                }}
                className="input neonInput"
              />
            </div>

            <div className="field">
              <label className="label">Category</label>
              <div className="select is-fullwidth neonSelectWrapper">
                <select
                  value={category}
                  onChange={(e) => setCategory(e.target.value)}
                  required
                >
                  <option value="">Select category</option>
                  <option value="Health">Health</option>
                  <option value="Education">Education</option>
                  <option value="Shopping">Shopping</option>
                  <option value="Savings">Savings</option>
                  <option value="Investment">Investment</option>
                  <option value="Salary">Salary</option>
                  <option value="Gift">Gift</option>
                  <option value="Food">Food</option>
                  <option value="Travel">Travel</option>
                  <option value="Bills">Bills</option>
                  <option value="Entertainment">Entertainment</option>
                  <option value="General">General</option>
                </select>
              </div>
            </div>

            <div className="field neonCheckboxField">
              <label className="checkbox">
                <input
                  type="checkbox"
                  checked={isRecurring}
                  onChange={(e) => setIsRecurring(e.target.checked)}
                />{" "}
                Recurring
              </label>
            </div>

            {isRecurring && (
              <div className="field">
                <label className="label">Recurring Frequency</label>
                <div className="select is-fullwidth neonSelectWrapper">
                  <select
                    value={recurringFrequency}
                    onChange={(e) => setRecurringFrequency(e.target.value)}
                    required
                  >
                    <option value="">Select frequency</option>
                    <option value="Daily">Daily</option>
                    <option value="Weekly">Weekly</option>
                    <option value="Monthly">Monthly</option>
                  </select>
                </div>
              </div>
            )}

            {isRecurring && recurringFrequency === "Monthly" && (
              <div className="field neonCheckboxField">
                <label className="checkbox">
                  <input
                    type="checkbox"
                    checked={isEMI}
                    onChange={(e) => setIsEMI(e.target.checked)}
                  />{" "}
                  This is an EMI
                </label>
              </div>
            )}

            {isEMI && (
              <div className="emiBox neonBox mt-4">
                <h4 className="title is-6">💳 EMI Details</h4>

                <input
                  className="input neonInput mb-2"
                  type="number"
                  placeholder="Principal (₹)"
                  value={emiPrincipal}
                  onChange={(e) => setEmiPrincipal(e.target.value)}
                />

                <input
                  className="input neonInput mb-2"
                  type="number"
                  placeholder="Annual Interest Rate (%)"
                  value={emiRate}
                  onChange={(e) => setEmiRate(e.target.value)}
                />

                <input
                  className="input neonInput mb-2"
                  type="number"
                  placeholder="Tenure (months)"
                  value={emiTenure}
                  onChange={(e) => setEmiTenure(e.target.value)}
                />

                <p>
                  📆 Monthly EMI: <strong>₹{emiAmount.toFixed(2)}</strong>
                </p>

                {!emiAffordable && (
                  <p className="has-text-danger mt-2">
                    🚫 EMI not affordable under current budget
                  </p>
                )}
              </div>
            )}

            <div className="field mt-4">
              <button className="neonButton" type="submit">
                {editingId ? "Update Transaction" : "Add Transaction"}
              </button>
            </div>
          </form>
        </div>

        <div className="searchWrapper">
          <input
            className="input neonInput"
            type="text"
            placeholder="Search by name, category, or description..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value.toLowerCase())}
          />
        </div>

        <div className="transactionsList">
          {uniqueTransactions.map((t) => (
            <div key={t._id} className="transactionCard neonBox">
              <div className="transactionDetails">
                <p className="transactionName">{t.name}</p>
                <p className="transactionDescription">{t.description}</p>
                <p className="transactionDatetime">
                  {new Date(t.datetime).toLocaleString()}
                </p>
                <span className="tag neonTag">{t.category}</span>
                {t.emiMeta && (
                  <div className="mt-2">
                    <p className="is-size-7">📆 EMI Progress</p>

                    {(() => {
                      const total =
                        t.emiMeta.originalTenure || t.emiMeta.tenureMonths;
                      const remaining = t.emiMeta.tenureMonths;
                      const paid = total - remaining;
                      const percent = total > 0 ? (paid / total) * 100 : 0;

                      return (
                        <>
                          <progress
                            className="progress is-small is-info"
                            value={percent}
                            max="100"
                          >
                            {percent}%
                          </progress>
                          <p className="is-size-7 has-text-grey">
                            {paid} / {total} months paid
                          </p>
                        </>
                      );
                    })()}
                  </div>
                )}
              </div>

              <div className="transactionActions">
                <p
                  className={`transactionPrice ${t.price < 0 ? "negativePrice" : "positivePrice"
                    }`}
                >
                  ₹{t.price}
                </p>
                <div className="buttons">
                  <button
                    className="button editBtn"
                    onClick={() => handleEdit(t)}
                  >
                    Edit
                  </button>
                  <button
                    className="button deleteBtn"
                    onClick={() => handleDelete(t._id)}
                  >
                    Delete
                  </button>
                  <button
                    className="button receiptBtn"
                    onClick={() => {
                      const url = `${API_URL}/transaction/receipt/${t._id}`;
                      const token = localStorage.getItem("token");

                      fetch(url, {
                        method: "GET",
                        headers: {
                          Authorization: `Bearer ${token}`,
                        },
                      })
                        .then((res) => res.blob())
                        .then((blob) => {
                          const link = document.createElement("a");
                          link.href = window.URL.createObjectURL(blob);
                          link.download = `receipt_${t._id}.pdf`;
                          link.click();
                        });
                    }}
                  >
                    Generate Receipt
                  </button>
                </div>
              </div>
            </div>
          ))}
        </div>
      </div>
      <Footer />
    </>
  );
}

export default App;
