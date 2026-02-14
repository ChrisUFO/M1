<!-- See COPYING.txt for license details. -->

# M1 Development Guidelines

## Coding Standards

- Follow STM32 HAL coding conventions
- Use meaningful variable and function names
- Prefer camelCase for variables/functions, UPPER_CASE for constants
- Use appropriate integer types (`uint8_t`, `uint16_t`, etc.)
- Add comments for non-obvious logic

## Branch Naming

- `feature/` – New features
- `fix/` – Bug fixes
- `docs/` – Documentation only
- `refactor/` – Code refactoring

## Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

- `feat:` – New feature
- `fix:` – Bug fix
- `docs:` – Documentation
- `style:` – Formatting
- `refactor:` – Code restructure
- `test:` – Tests
- `chore:` – Maintenance

## Testing

- Test on hardware when possible
- Document test scenarios and edge cases
- Ensure NFC, RFID, and Sub-GHz functionality are verified
