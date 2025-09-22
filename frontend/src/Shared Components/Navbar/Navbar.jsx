import { Link, useNavigate } from "react-router-dom";
import { useEffect, useState } from "react";
import "./Navbar.css";
import appLogo from "../../assets/App logo.png"; // adjust path if needed

function Navbar() {
  const [isLoggedIn, setIsLoggedIn] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const navigate = useNavigate();

  useEffect(() => {
    const token = localStorage.getItem("token");
    if (!token) {
      navigate("/login");
    } else {
      setIsLoggedIn(true);
    }
  }, [navigate]);

  function handleLogout() {
    localStorage.removeItem("token");
    setIsLoggedIn(false);
    navigate("/login");
  }

  return (
    <nav className="neon-navbar">
      <div className="nav-logo">
        <Link to="/">
          <img src={appLogo} alt="FinTally Logo" className="logo-img" />
          <span>Fintally</span>
        </Link>
      </div>

      {/* Hamburger Menu Icon */}
      <div
        className={`hamburger ${menuOpen ? "active" : ""}`}
        onClick={() => setMenuOpen(!menuOpen)}
      >
        <span></span>
        <span></span>
        <span></span>
      </div>

      {/* Links */}
      <div className={`nav-links ${menuOpen ? "open" : ""}`}>
        {isLoggedIn ? (
          <>
            <Link to="/" onClick={() => setMenuOpen(false)}>Home</Link>
            <Link to="/insights" onClick={() => setMenuOpen(false)}>Insights</Link>
            <Link to="/transactions" onClick={() => setMenuOpen(false)}>Transactions</Link>
            <Link to="/about" onClick={() => setMenuOpen(false)}>About</Link>
            <Link to="/feedback" onClick={() => setMenuOpen(false)}>Feedback</Link>
            <button className="logout-btn" onClick={() => { handleLogout(); setMenuOpen(false); }}>
              Logout
            </button>
          </>
        ) : (
        <>
          <Link to="/login" onClick={() => setMenuOpen(false)}>Login</Link>
          <Link to="/signup" onClick={() => setMenuOpen(false)}>Signup</Link>
        </>
        )}
      </div>
    </nav>
  );
}

export default Navbar;
