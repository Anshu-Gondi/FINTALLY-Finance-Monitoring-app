import { Link } from "react-router-dom";
import { useEffect, useState } from "react";
import { auth } from "../../services/api";
import "./Login.css";

const Login = () => {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  async function handleLogin(e) {
    e.preventDefault();
    setError("");
    setLoading(true);
    try {
      const data = await auth.login(email, password);
      localStorage.setItem("token", data.token);
      window.location.href = "/";
    } catch (err) {
      setError(err.message || "Login failed");
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    if (!window.google) return;
    window.google.accounts.id.initialize({
      client_id: import.meta.env.VITE_GOOGLE_CLIENT_ID,
      callback: async (response) => {
        try {
          const data = await auth.googleAuth(response.credential);
          localStorage.setItem("token", data.token);
          window.location.href = "/";
        } catch (err) {
          setError(err.message || "Google login failed");
        }
      },
    });
    window.google.accounts.id.renderButton(
      document.getElementById("googleSignInDiv"),
      { theme: "filled_black", size: "large" }
    );
  }, []);

  return (
    <div className="loginContainer">
      <form className="loginForm neonBox" onSubmit={handleLogin}>
        <h2 className="loginTitle">Login</h2>

        {error && <p className="has-text-danger mb-3">{error}</p>}

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

        <button className="neonButton" type="submit" disabled={loading}>
          {loading ? "Logging in…" : "Login"}
        </button>

        <div className="divider"><span>or continue with</span></div>
        <div className="googleWrapper">
          <div id="googleSignInDiv" />
        </div>

        <p className="signupLink">
          Don &apos; t have an account?{" "}
          <Link to="/signup" className="signupHighlight">Sign up here</Link>
        </p>
      </form>
    </div>
  );
};

export default Login;