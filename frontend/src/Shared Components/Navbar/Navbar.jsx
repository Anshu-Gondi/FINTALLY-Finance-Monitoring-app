import { Link, useNavigate, useLocation } from "react-router-dom";
import { useEffect, useState } from "react";
import "./Navbar.css";
import appLogo from "../../assets/App logo.png"; // adjust path if needed

function Navbar() {
  const [isLoggedIn, setIsLoggedIn] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const navigate = useNavigate();
  const location = useLocation();

  useEffect(() => {
    const token = localStorage.getItem("token");
    if (token) {
      setIsLoggedIn(true);
    } else {
      setIsLoggedIn(false);
      // Only redirect to login if the current path is a protected route
      const protectedRoutes = ["/insights", "/transactions", "/chatbot"];
      if (protectedRoutes.includes(location.pathname)) {
        navigate("/login");
      }
    }
  }, [navigate, location.pathname]);

  function handleLogout() {
    localStorage.removeItem("token");
    setIsLoggedIn(false);
    navigate("/login");
  }

  const closeMenu = () => setMenuOpen(false);

  return (
    <nav className="neon-navbar">
      <div className="nav-logo">
        <Link to="/" onClick={closeMenu}>
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

      {/* Navigation Links */}
      <div className={`nav-links ${menuOpen ? "open" : ""}`}>
        {isLoggedIn ? (
          <>
            <Link to="/" onClick={closeMenu}>Home</Link>
            <Link to="/insights" onClick={closeMenu}>Insights</Link>
            <Link to="/transactions" onClick={closeMenu}>Transactions</Link>
            <Link to="/chatbot" onClick={closeMenu}>AI Assistant</Link>
            <Link to="/about" onClick={closeMenu}>About</Link>
            <Link to="/feedback" onClick={closeMenu}>Feedback</Link>
            <button
              className="logout-btn"
              onClick={() => {
                handleLogout();
                closeMenu();
              }}
            >
              Logout
            </button>
          </>
        ) : (
          <>
            <Link to="/about" onClick={closeMenu}>About</Link>
            <Link to="/feedback" onClick={closeMenu}>Feedback</Link>
            <Link to="/login" onClick={closeMenu}>Login</Link>
            <Link to="/signup" onClick={closeMenu}>Signup</Link>
          </>
        )}
      </div>
    </nav>
  );
}

export default Navbar;
