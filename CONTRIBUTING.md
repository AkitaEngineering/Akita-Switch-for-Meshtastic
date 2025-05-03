# Contributing to Akita Switch for Meshtastic

Thank you for your interest in contributing! We welcome contributions from the community to improve the Akita Switch project.

## How to Contribute

* **Reporting Bugs:** If you find a bug, please open an issue on the GitHub repository. Include detailed steps to reproduce the bug, the expected behavior, and the actual behavior. Please also include versions of relevant software (Python, meshtastic library, Arduino IDE, ESP32 core version) and hardware used (ESP32 board, Host OS, Meshtastic device model).
* **Suggesting Enhancements:** Open an issue to suggest new features or improvements. Explain the enhancement clearly and discuss why it would be beneficial to the project.
* **Pull Requests:** If you'd like to contribute code or documentation improvements:
    1.  Fork the repository on GitHub.
    2.  Create a new branch for your feature or bug fix (e.g., `git checkout -b feature/add-mqtt-support` or `git checkout -b fix/i2c-timeout-logic`).
    3.  Make your changes in your branch. Ensure code is well-commented and generally follows the existing style. Add relevant tests if applicable.
    4.  Test your changes thoroughly to ensure they work as expected and don't introduce regressions.
    5.  Commit your changes with clear and descriptive commit messages (`git commit -am 'feat: Add basic MQTT publishing for sensor data'`).
    6.  Push the branch to your fork on GitHub (`git push origin feature/add-mqtt-support`).
    7.  Open a Pull Request (PR) from your branch back to the main Akita Engineering repository. Provide a clear title and description for your PR, explaining the changes and referencing any related issues.

## Development Guidelines

* **Python:** Aim to follow PEP 8 style guidelines. Use meaningful variable and function names. Add docstrings to functions and classes.
* **Arduino (C++):** Follow standard C++ practices and the Arduino style guide where applicable. Use meaningful names and add comments to explain complex logic sections. Ensure GPLv3 license headers are present.
* **Commit Messages:** Write clear and concise commit messages, explaining *what* changed and *why*.
* **License:** All contributions must be licensed under the GNU General Public License v3.0 (GPLv3), consistent with the project's license.

Thank you for contributing!

---
**Akita Engineering**
*Email:* info@akitaengineering.com
*Website:* www.akitaengineering.com
