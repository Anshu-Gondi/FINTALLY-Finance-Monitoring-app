import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import "./Signup.css";

const Signup = () => {
  const [name, setName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  async function handleSignup(e) {
    e.preventDefault();
    try {
      const res = await fetch(`${import.meta.env.VITE_API_URL}/signup`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, email, password }),
      });
      const data = await res.json();
      if (data.success) {
        alert("Signup successful!");
        window.location.href = "/login";
      } else {
        alert(data.message || "Signup failed");
      }
    } catch (err) {
      console.error("Signup error:", err);
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
              alert("Google signup/login successful!");
              window.location.href = "/";
            } else {
              alert(data.message || "Google signup/login failed");
            }
          } catch (err) {
            console.error("Google signup error:", err);
          }
        },
      });

      window.google.accounts.id.renderButton(
        document.getElementById("googleSignUpDiv"),
        { theme: "filled_blue", size: "large", width: "100%" }
      );
    }
  }, []);

  return (
    <div className="signupContainer">
      <form className="signupForm" onSubmit={handleSignup} noValidate>
        <h2 className="signupTitle">Create your FinTally Account</h2>

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

        <button className="submitBtn" type="submit">
          Sign Up
        </button>

        <div className="divider">
          <span>or continue with</span>
        </div>

        <div id="googleSignUpDiv" className="googleButtonWrapper"></div>

        {/* Login Link */}
        <p className="loginLink">
          Already have an account?{" "}
          <Link to="/login" className="loginHighlight">
            Login here
          </Link>
        </p>
      </form>
    </div>
  );
};

export default Signup;
