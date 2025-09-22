import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Routes, Route } from "react-router-dom";
import './index.css'
import App from './App.jsx'
import Home from './Components/Home page/Home.jsx';
import Login from "./Components/Login Page/Login.jsx";
import Signup from "./Components/Signup Page/Signup.jsx";
import InsightsPage from "./Components/Insights Page/Insights.jsx";
import ChatbotPage from "./Components/chatbot page/ChatbotPage.jsx";
import About from "./Components/About page/About.jsx";
import Feedback from './Components/Feedback page/Feedback.jsx';
import 'bulma/css/bulma.min.css';

createRoot(document.getElementById("root")).render(
  <StrictMode>
    <BrowserRouter>
      <Routes>
        <Route path="/transactions" element={<App />} />
        <Route path="/" element={<Home />} />
        <Route path="/login" element={<Login />} />
        <Route path="/signup" element={<Signup />} />
        <Route path="/about" element={<About />} />
        <Route path="/feedback" element={<Feedback />} />
        <Route path="/insights" element={<InsightsPage />} />
        <Route path="/chatbot" element={<ChatbotPage />} />
      </Routes>
    </BrowserRouter>
  </StrictMode>
);
