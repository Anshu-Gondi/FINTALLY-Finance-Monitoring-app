const express = require("express");
const router = express.Router();
const Transaction = require("../models/Transaction");
const jwt = require("jsonwebtoken");
const multer = require("multer");
const path = require("path");
const fs = require("fs");
const PDFDocument = require("pdfkit");
const authMiddleware = require("../middlewares/auth");

// Create uploads dir if not exists
const uploadDir = path.join(__dirname, "../uploads");
if (!fs.existsSync(uploadDir)) fs.mkdirSync(uploadDir);

// Multer config
const storage = multer.diskStorage({
  destination: (req, file, cb) => cb(null, uploadDir),
  filename: (req, file, cb) => cb(null, `${Date.now()}_${file.originalname}`),
});
const upload = multer({ storage });

// 🧪 Test
router.get("/test", (req, res) => {
  res.json({ body: "test ok" });
});

// 🟢 Create Transaction
router.post("/", authMiddleware, upload.single("receipt"), async (req, res) => {
  try {
    const {
      name,
      price,
      description,
      datetime,
      category,
      isRecurring,
      recurringFrequency,
    } = req.body;

    const newTransaction = new Transaction({
      name,
      price,
      description,
      datetime,
      category,
      isRecurring,
      recurringFrequency,
      userId: req.user.userId,
    });

    if (req.file) {
      newTransaction.receiptUrl = `/uploads/${req.file.filename}`;
    }

    const saved = await newTransaction.save();
    res.json({ success: true, data: saved });
  } catch (error) {
    console.error("Error saving transaction:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

// 🟠 Get Transactions (Paginated)
router.get("/", authMiddleware, async (req, res) => {
  try {
    const page = parseInt(req.query.page) || 1;
    const limit = 10;
    const skip = (page - 1) * limit;

    const transactions = await Transaction.find({ userId: req.user.userId })
      .sort({ datetime: -1 })
      .skip(skip)
      .limit(limit);

    res.json({ success: true, data: transactions });
  } catch (error) {
    console.error("Error fetching transactions:", error);
    res
      .status(500)
      .json({ success: false, message: "Failed to fetch transactions" });
  }
});

// 🟡 Update Transaction
router.put(
  "/:id",
  authMiddleware,
  upload.single("receipt"),
  async (req, res) => {
    try {
      const { id } = req.params;
      const {
        name,
        price,
        description,
        datetime,
        category,
        isRecurring,
        recurringFrequency,
      } = req.body;

      const existing = await Transaction.findOne({
        _id: id,
        userId: req.user.userId,
      });
      if (!existing)
        return res
          .status(404)
          .json({ success: false, message: "Transaction not found" });

      // Remove old receipt if uploading new one
      if (req.file && existing.receiptUrl) {
        const oldPath = path.join(__dirname, "..", existing.receiptUrl);
        if (fs.existsSync(oldPath)) fs.unlinkSync(oldPath);
      }

      const updated = await Transaction.findOneAndUpdate(
        { _id: id, userId: req.user.userId },
        {
          name,
          price,
          description,
          datetime,
          category,
          isRecurring,
          recurringFrequency,
          ...(req.file && { receiptUrl: `/uploads/${req.file.filename}` }),
        },
        { new: true }
      );

      res.json({ success: true, data: updated });
    } catch (error) {
      console.error("Error updating transaction:", error);
      res.status(500).json({ success: false, message: "Server error" });
    }
  }
);

// 🔴 Delete Transaction
router.delete("/:id", authMiddleware, async (req, res) => {
  try {
    const transaction = await Transaction.findOne({
      _id: req.params.id,
      userId: req.user.userId,
    });

    if (!transaction) {
      return res
        .status(404)
        .json({ success: false, message: "Transaction not found" });
    }

    if (transaction.receiptUrl) {
      const receiptPath = path.join(__dirname, "..", transaction.receiptUrl);
      if (fs.existsSync(receiptPath)) fs.unlinkSync(receiptPath);
    }

    await Transaction.deleteOne({
      _id: req.params.id,
      userId: req.user.userId,
    });

    res.json({ success: true, message: "Transaction deleted successfully" });
  } catch (error) {
    console.error("Error deleting transaction:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

// 🧾 Generate Branded PDF Receipt
router.get("/receipt/:id", authMiddleware, async (req, res) => {
  try {
    const transaction = await Transaction.findOne({
      _id: req.params.id,
      userId: req.user.userId,
    });

    if (!transaction) {
      return res
        .status(404)
        .json({ success: false, message: "Transaction not found" });
    }

    const PDFDocument = require("pdfkit");
    const path = require("path");
    const doc = new PDFDocument({ margin: 50 });

    res.setHeader("Content-Type", "application/pdf");
    res.setHeader(
      "Content-Disposition",
      `attachment; filename=receipt_${transaction._id}.pdf`
    );
    doc.pipe(res);

    // 🖼 Logo + Brand Header
    const logoPath = path.resolve(__dirname, "../../api/assets/App logo.png");
    doc.rect(0, 0, doc.page.width, 80).fill("#0d0d0d");

    doc.image(logoPath, 50, 20, { width: 50 });
    doc
      .fillColor("#00f0ff")
      .fontSize(26)
      .font("Helvetica-Bold")
      .text("FinTally", 110, 28);

    doc.fillColor("#888").fontSize(10).text("https://yourdomain.com", 110, 55);

    // Divider
    doc.moveTo(50, 85).lineTo(550, 85).strokeColor("#00f0ff").stroke();

    // Watermark - place higher so it doesn't push content
    doc
      .fontSize(80)
      .fillColor("#f0f0f0")
      .opacity(0.1)
      .text("RECEIPT", 100, 150, { angle: 45 }); // was y=300, now y=150

    // Title - set position manually to remove gap
    doc
      .opacity(1)
      .fillColor("#000")
      .fontSize(20)
      .text("Transaction Receipt", 50, 100, {
        align: "center",
        underline: true,
      }); // y=100 is just under header
    doc.moveDown(1); // was 2

    // Transaction Details Table
    const addRow = (label, value, color = "#000") => {
      doc
        .font("Helvetica-Bold")
        .fillColor("#00f0ff")
        .text(`${label}: `, { continued: true })
        .font("Helvetica")
        .fillColor(color)
        .text(value);
      doc.moveDown(0.3);
    };

    doc
      .rect(45, doc.y - 5, 510, 1) // top border
      .fill("#00f0ff");

    addRow("Transaction ID", transaction._id.toString());
    addRow("Date/Time", new Date(transaction.datetime).toLocaleString());
    addRow("Category", transaction.category);
    addRow("Description", transaction.description || "N/A");
    addRow("Recurring", transaction.isRecurring ? "Yes" : "No");
    if (transaction.isRecurring) {
      addRow("Frequency", transaction.recurringFrequency);
    }
    addRow("Amount", `₹${transaction.price.toFixed(2)}`, "#c70000");

    doc
      .rect(45, doc.y, 510, 1) // bottom border
      .fill("#00f0ff");

    doc.moveDown(3);

    // Footer
    doc
      .fontSize(10)
      .fillColor("#888")
      .text("Thank you for using FinTally!", { align: "center" })
      .moveDown(0.3)
      .text(
        "This receipt is computer generated and does not require a signature.",
        { align: "center" }
      );

    doc.end();
  } catch (error) {
    console.error("Receipt generation failed:", error);
    res
      .status(500)
      .json({ success: false, message: "Could not generate receipt" });
  }
});

module.exports = router;
