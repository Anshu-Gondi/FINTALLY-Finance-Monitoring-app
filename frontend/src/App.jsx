import { useEffect, useState, useCallback } from "react";
import "bulma/css/bulma.min.css";
import "./App.css";
import Flatpickr from "react-flatpickr";
import "flatpickr/dist/themes/dark.css";
import Modal from "react-modal";
import Navbar from "./Shared Components/Navbar/Navbar";
import Footer from "./Shared Components/Footer/Footer";

Modal.setAppElement("#root");
let scrollThrottleTimeout = null;

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

  const BUDGET_LIMIT = 5000;

  const API_URL = import.meta.env.VITE_API_URL;

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
    [API_URL]
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
      new Map(transactions.map((t) => [t._id, t])).values()
    );
    const total = uniqueTx.reduce((acc, t) => acc + t.price, 0);
    setBalance(total);
  }, [transactions]);

  const addNewTransaction = async (ev) => {
    ev.preventDefault();

    const formData = new FormData();
    formData.append("name", name);
    formData.append("price", price);
    formData.append("description", description);
    formData.append("datetime", datetime);
    formData.append("category", category);
    formData.append("isRecurring", isRecurring);
    if (isRecurring) formData.append("recurringFrequency", recurringFrequency);
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
        prev.map((tx) => (tx._id === editingId ? updated.data : tx))
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
  };

  const handleDelete = async (id) => {
    const confirm = window.confirm(
      "Are you sure you want to delete this transaction?"
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
  };

  const filteredTransactions = transactions.filter(
    (t) =>
      t.name?.toLowerCase().includes(searchQuery) ||
      t.description?.toLowerCase().includes(searchQuery) ||
      t.category?.toLowerCase().includes(searchQuery)
  );

  const uniqueTransactions = Array.from(
    new Map(filteredTransactions.map((t) => [t._id, t])).values()
  );

  useEffect(() => {
    const handleScroll = () => {
      if (scrollThrottleTimeout) return;
      scrollThrottleTimeout = setTimeout(() => {
        if (
          window.innerHeight + window.scrollY >=
          document.body.offsetHeight - 50 &&
          hasMore
        ) {
          setPage((prev) => prev + 1);
        }
        scrollThrottleTimeout = null;
      }, 300); // throttle to 300ms
    };

    window.addEventListener("scroll", handleScroll);
    return () => window.removeEventListener("scroll", handleScroll);
  }, [hasMore]);

  return (
    <>
      <Navbar />
      <div className="appContainer">
        <h1 className="appTitle">💸 FinTally</h1>
        <h2 className="appBalance">
          Balance: ₹{balance.toFixed(2)}
        </h2>

        {balance < -BUDGET_LIMIT && (
          <p className="budgetAlert">
            🚨 Budget exceeded by ₹{Math.abs(balance + BUDGET_LIMIT).toFixed(2)}!
          </p>
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
                onChange={(e) => setPrice(Number(e.target.value))}
                placeholder="Amount (negative for expense)"
                required
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
