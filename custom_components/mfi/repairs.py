"""Actionable issues without modifying another integration's resources."""

from homeassistant.core import HomeAssistant
from homeassistant.helpers import issue_registry as ir

from .const import DOMAIN


def set_issue(hass: HomeAssistant, entry_id: str, kind: str, detail: str | None) -> None:
    """Create or clear an entry-scoped repair notice."""
    issue_id = f"{entry_id}_{kind}"
    if detail is None:
        ir.async_delete_issue(hass, DOMAIN, issue_id)
        return
    ir.async_create_issue(
        hass,
        DOMAIN,
        issue_id,
        is_fixable=False,
        severity=ir.IssueSeverity.ERROR if kind == "storage" else ir.IssueSeverity.WARNING,
        translation_key=kind,
        translation_placeholders={"detail": detail},
    )
