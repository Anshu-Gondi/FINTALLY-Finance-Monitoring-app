const { Schema, model } = require("mongoose");

const UserSchema = new Schema({
  name: { type: String, required: true },
  email: { type: String, required: true, unique: true },
  password: { type: String},
  authProvider: { type: String, default: "local" },
});

const UserModel = model("User", UserSchema);
module.exports = UserModel;
