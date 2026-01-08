const express = require("express");
const path = require("path");
const app = express();
const port = 3000;
const Transaction = require("./models/Transaction");
require("dotenv").config();
const cors = require("cors");
const mongoose = require("mongoose");
const { Server } = require("http");

// Import routes
const authRoutes = require("./routes/auth");
const transactionRoutes = require("./routes/Transaction_route");
const insightRoutes = require("./routes/insights");
const chatbotRoutes = require("./routes/chatbot");
const feedbackRoutes = require("./routes/Feedback");
const budgetRoutes = require("./routes/budget");

app.use(
  cors({
    origin: "http://localhost:5173", // your frontend
    credentials: true,               // allow cookies/headers
  })
);
app.use(express.json());

// ✅ Serve static files for uploaded receipts
app.use("/uploads", express.static(path.join(__dirname, "uploads")));

// Connect to MongoDB once when the server starts
mongoose
  .connect(process.env.MONGO_URL)
  .then(() => console.log("Connected to MongoDB"))
  .catch((error) => console.error("MongoDB connection error:", error));

// Test endpoint
app.get("/api/test", (req, res) => {
  res.json({ body: "test ok" });
});

// Transaction endpoint
app.use("/api/transaction", transactionRoutes);

// Login and Signup endpoint
app.use("/api", authRoutes); // Will handle /api/signup and /api/login

// Insights endpoint
app.use("/api", insightRoutes); // Handles /api/insights/*

// Chatbot endpoint
app.use("/api", chatbotRoutes);

// Feedback endpoint
app.use("/api", feedbackRoutes);

// Budget endpoint
app.use("/api/budget", budgetRoutes);

// Start the server
app.listen(port, () => {
  console.log(`Server is running on http://localhost:${port}`);

  // ✅ Start recurring transaction job
  require("./cron jobs/recurringJobs"); // 👈 This will register your cron job
});
