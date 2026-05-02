import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { auth } from "../../services/api";
import "./Signup.css";

const Signup = () => {
  const [name, setName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  async function handleSignup(e) {
    e.preventDefault();
    setError("");
    setLoading(true);
    try {
      await auth.signup(name, email, password);
      window.location.href = "/login";
    } catch (err) {
      setError(err.message || "Signup failed");
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
          setError(err.message || "Google signup failed");
        }
      },
    });
    window.google.accounts.id.renderButton(
      document.getElementById("googleSignUpDiv"),
      { theme: "filled_blue", size: "large", width: "100%" }
    );
  }, []);

  return (
    <div className="signupContainer">
      <form className="signupForm" onSubmit={handleSignup} noValidate>
        <h2 className="signupTitle">Create your FinTally Account</h2>

        {error && <p className="has-text-danger mb-3">{error}</p>}

        <label className="inputLabel" htmlFor="name">Name</label>
        <input
          id="name"
          className="inputField"
          value={name}
          onChange={(e) => setName(e.target.value)}
          placeholder="Enter your full name"
          required
          autoComplete="name"
        />

        <label className="inputLabel" htmlFor="email">Email Address</label>
        <input
          id="email"
          type="email"
          className="inputField"
          value={email}
          onChange={(e) => setEmail(e.target.value)}
          placeholder="your.email@example.com"
          required
          autoComplete="email"
        />

        <label className="inputLabel" htmlFor="password">Password</label>
        <input
          id="password"
          type="password"
          className="inputField"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          placeholder="Create a strong password"
          required
          autoComplete="new-password"
        />

        <button className="submitBtn" type="submit" disabled={loading}>
          {loading ? "Creating account…" : "Sign Up"}
        </button>

        <div className="divider"><span>or continue with</span></div>
        <div id="googleSignUpDiv" className="googleButtonWrapper" />

        <p className="loginLink">
          Already have an account?{" "}
          <Link to="/login" className="loginHighlight">Login here</Link>
        </p>
      </form>
    </div>
  );
};

export default Signup;