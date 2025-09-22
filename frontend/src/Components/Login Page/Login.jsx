import { Link } from "react-router-dom";
import { useEffect, useState } from "react";
import "./Login.css";

const Login = () => {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  async function handleLogin(e) {
    e.preventDefault();
    try {
      const res = await fetch(`${import.meta.env.VITE_API_URL}/login`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ email, password }),
      });
      const data = await res.json();
      if (data.success && data.token) {
        localStorage.setItem("token", data.token);
        alert("Login successful!");
        window.location.href = "/";
      } else {
        alert(data.message || "Login failed");
      }
    } catch (err) {
      console.error("Login error:", err);
      alert("Server error");
    }
  }

  useEffect(() => {
    if (window.google) {
      window.google.accounts.id.initialize({
        client_id: import.meta.env.VITE_GOOGLE_CLIENT_ID,
        callback: async (response) => {
          try {
            const res = await fetch(`${import.meta.env.VITE_API_URL}/google-auth`, {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ token: response.credential }),
            });
            const data = await res.json();
            if (data.success && data.token) {
              localStorage.setItem("token", data.token);
              alert("Google login successful!");
              window.location.href = "/";
            } else {
              alert(data.message || "Google login failed");
            }
          } catch (err) {
            console.error("Google login error:", err);
          }
        },
      });

      window.google.accounts.id.renderButton(
        document.getElementById("googleSignInDiv"),
        { theme: "filled_black", size: "large" }
      );
    }
  }, []);

  return (
    <div className="loginContainer">
      <form className="loginForm neonBox" onSubmit={handleLogin}>
        <h2 className="loginTitle">Login</h2>
        <div className="field">
          <input
            className="input neonInput"
            type="email"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            placeholder="Email"
            required
          />
        </div>
        <div className="field">
          <input
            className="input neonInput"
            type="password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            placeholder="Password"
            required
          />
        </div>
        <button className="neonButton" type="submit">
          Login
        </button>

        <div className="divider">
          <span>or continue with</span>
        </div>

        <div className="googleWrapper">
          <div id="googleSignInDiv"></div>
        </div>

        {/* Signup Link */}
        <p className="signupLink">
          Don’t have an account?{" "}
          <Link to="/signup" className="signupHighlight">
            Sign up here
          </Link>
        </p>
      </form>
    </div>
  );
};

export default Login;
