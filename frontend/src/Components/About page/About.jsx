import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import profileImg from "../../assets/profile-preview.jpeg"; // replace with your own image
import "./About.css";

export default function About() {
  return (
    <div className="about-bg min-h-screen flex flex-col text-white">
      <Navbar />

      <section className="about-section flex-1 px-6 py-12">
        <div className="about-container">
          <div className="about-image">
            <img src={profileImg} alt="Profile" className="profile-img" />
          </div>
          <div className="about-content">
            <h1 className="neon-title">About Me</h1>
            <p className="about-text">
              Hi, I’m <span className="highlight">Anshu Gondi</span> — the creator of{" "}
              <span className="highlight">FinTally</span>.  
              I’m passionate about building tools that make personal finance simple, visual, and even a little fun.  
              With a background in <span className="highlight">web development</span> and a love for clean UI design,  
              I aim to create applications that feel smooth, fast, and futuristic.
            </p>
            <p className="about-text">
              This project combines modern tech with a neon aesthetic to keep you engaged while tracking  
              your income, expenses, and insights. My mission is to help you stay on top of your  
              financial goals without the stress.
            </p>
            <p className="about-text">
              When I’m not coding, you’ll probably find me exploring UI animations,  
              reading tech blogs, or sipping coffee while sketching my next big project idea.
            </p>
          </div>
        </div>
      </section>

      <Footer />
    </div>
  );
}
