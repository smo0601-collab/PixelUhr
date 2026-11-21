// PixelUhr features: page sequence editor, scrolling texts, calendar,
// alarms and extras. Uses helpers from app.js ($, tr, trFormat, apiFetch,
// message, currentLanguage, bindAction). Each editor saves through its own
// JSON endpoint; the global save bar only covers the regular settings form.

const pageTypeKeys = ["clock", "date", "weather", "weatherinfo", "texts", "events", "week", "sun", "moon", "quote", "news", "greeting", "wifi", "warning", "animation"];
const pageTypeLabels = {
  clock: "Uhrzeit", date: "Datum", weather: "Wetter", weatherinfo: "Wetterdetails", texts: "Lauftexte",
  events: "Termine und Feiertage", week: "Kalenderwoche", sun: "Sonnenaufgang und -untergang", moon: "Mondphase",
  quote: "Spruch des Tages", news: "Nachrichten", greeting: "Begrüßung", wifi: "WLAN-Signal",
  warning: "Wetterwarnungen", animation: "Animation"
};
const eventTypeIcons = ["icon-calendar", "icon-cake", "icon-trash", "icon-hourglass", "icon-suitcase", "icon-bell"];
const eventTypeNames = ["Termin", "Geburtstag", "Müllabfuhr", "Countdown", "Urlaub", "Erinnerung"];
const repeatNames = ["Einmalig", "Jährlich", "Monatlich", "Wöchentlich"];
const dayOrder = [1, 2, 3, 4, 5, 6, 0];
const dayNamesDe = ["So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"];
const dayNamesEn = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"];

const feature = {
  loaded: false,
  texts: [],
  events: [],
  alarms: [],
  extras: {},
  saved: { texts: "[]", events: "[]", alarms: "[]", extras: "{}" },
  upcoming: [],
  holidays: [],
  news: { items: [], error: "" },
  editingText: -1,
  editingEvent: -1,
  eventsFetchedAt: 0,
  maxTexts: 12,
  maxEvents: 40,
  maxAlarms: 6
};

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

function dayNames() {
  return currentLanguage === "de" ? dayNamesDe : dayNamesEn;
}

function pad2(value) {
  return String(value).padStart(2, "0");
}

function parseIsoDate(value) {
  const match = /^(\d{4})-(\d{2})-(\d{2})/.exec(value || "");
  return match ? new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3])) : null;
}

function isoDate(date) {
  return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())}`;
}

function formatDateText(value, withWeekday = false) {
  const date = typeof value === "string" ? parseIsoDate(value) : value;
  if (!date) return "";
  return date.toLocaleDateString(currentLanguage === "de" ? "de-DE" : "en-GB",
    withWeekday ? { weekday: "short", day: "2-digit", month: "2-digit", year: "numeric" } : { day: "2-digit", month: "2-digit", year: "numeric" });
}

function deviceToday() {
  const local = typeof lastStatus === "object" && lastStatus?.localTime;
  const date = local ? parseIsoDate(local) : null;
  const today = date || new Date();
  today.setHours(0, 0, 0, 0);
  return today;
}

function daysBetween(from, to) {
  return Math.round((Date.UTC(to.getFullYear(), to.getMonth(), to.getDate()) -
    Date.UTC(from.getFullYear(), from.getMonth(), from.getDate())) / 86400000);
}

// Mirrors cal::nextOccurrence() in src/calendar_logic.h.
function nextOccurrence(event, today) {
  const start = parseIsoDate(event.date);
  if (!start) return null;
  const repeat = Number(event.repeat) || 0;
  const interval = Math.max(1, Number(event.interval) || 1);
  if (start >= today || repeat === 0) return start >= today ? start : null;
  if (repeat === 1) {
    for (let year = today.getFullYear(); year <= today.getFullYear() + 1; year++) {
      const leap = (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
      const day = start.getMonth() === 1 && start.getDate() === 29 && !leap ? 28 : start.getDate();
      const candidate = new Date(year, start.getMonth(), day);
      if (candidate >= today) return candidate;
    }
    return null;
  }
  if (repeat === 2) {
    const startIndex = start.getFullYear() * 12 + start.getMonth();
    let offset = today.getFullYear() * 12 + today.getMonth() - startIndex;
    if (offset % interval) offset += interval - (offset % interval);
    for (let i = 0; i < 3; i++, offset += interval) {
      const month = startIndex + offset;
      const year = Math.floor(month / 12), m = month % 12;
      const last = new Date(year, m + 1, 0).getDate();
      const candidate = new Date(year, m, Math.min(start.getDate(), last));
      if (candidate >= today) return candidate;
    }
    return null;
  }
  const period = 7 * interval;
  const steps = Math.ceil(daysBetween(start, today) / period);
  const result = new Date(start);
  result.setDate(result.getDate() + steps * period);
  return result;
}

async function featureFetch(url) {
  const res = await apiFetch(url);
  if (!res.ok) throw new Error(`${url} ${res.status}`);
  return res.json();
}

async function featurePost(url, body) {
  const res = await apiFetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || tr("Speichern fehlgeschlagen."));
  return data;
}

// ---- Page sequence ---------------------------------------------------------

function fillSelectedPageOptions() {
  const select = $("selectedPage");
  const value = select.value || savedConfig?.selectedPage || "0";
  select.innerHTML = pageTypeKeys.map((key, index) => `<option value="${index}">${escapeHtml(tr(pageTypeLabels[key]))}</option>`).join("");
  select.value = String(value);
}

function readPageOrder() {
  const entries = [];
  for (const item of ($("pageOrder").value || "").split(",")) {
    const [key, seconds] = item.split(":");
    if (pageTypeKeys.includes(key) && !entries.some((entry) => entry.key === key)) {
      entries.push({ key, seconds: Math.max(0, Math.min(120, Number(seconds) || 0)), enabled: true });
    }
  }
  for (const key of pageTypeKeys) {
    if (!entries.some((entry) => entry.key === key)) entries.push({ key, seconds: 0, enabled: false });
  }
  return entries;
}

let pageOrderEntries = [];

function writePageOrder() {
  const enabled = pageOrderEntries.filter((entry) => entry.enabled);
  if (!enabled.length) {
    pageOrderEntries.find((entry) => entry.key === "clock").enabled = true;
    return writePageOrder();
  }
  $("pageOrder").value = enabled.map((entry) => `${entry.key}:${entry.seconds}`).join(",");
}

function renderPageOrderEditor() {
  fillSelectedPageOptions();
  pageOrderEntries = readPageOrder();
  const list = $("pageOrderEditor");
  list.innerHTML = pageOrderEntries.map((entry, index) => `
    <div class="pageOrderRow${entry.enabled ? "" : " isOff"}" data-index="${index}">
      <label class="pageOrderToggle"><input type="checkbox" data-role="enabled" ${entry.enabled ? "checked" : ""}><span>${escapeHtml(tr(pageTypeLabels[entry.key]))}</span></label>
      <label class="pageOrderSeconds"><input type="number" min="0" max="120" inputmode="numeric" data-role="seconds" value="${entry.seconds}" aria-label="${escapeHtml(tr("Dauer in Sekunden"))}"><span>s</span></label>
      <button class="iconButton ghost small" type="button" data-role="up" title="${escapeHtml(tr("Nach oben"))}" aria-label="${escapeHtml(tr("Nach oben"))}" ${index === 0 ? "disabled" : ""}><svg class="icon" aria-hidden="true"><use href="#icon-up"></use></svg></button>
      <button class="iconButton ghost small" type="button" data-role="down" title="${escapeHtml(tr("Nach unten"))}" aria-label="${escapeHtml(tr("Nach unten"))}" ${index === pageOrderEntries.length - 1 ? "disabled" : ""}><svg class="icon" aria-hidden="true"><use href="#icon-down"></use></svg></button>
    </div>`).join("");
}

function initPageOrderEditor() {
  const list = $("pageOrderEditor");
  const update = (event) => {
    const row = event.target.closest(".pageOrderRow");
    if (!row) return;
    const entry = pageOrderEntries[Number(row.dataset.index)];
    const role = event.target.dataset.role;
    if (role === "enabled") {
      entry.enabled = event.target.checked;
      row.classList.toggle("isOff", !entry.enabled);
    } else if (role === "seconds") {
      entry.seconds = Math.max(0, Math.min(120, Number(event.target.value) || 0));
    }
    writePageOrder();
  };
  list.addEventListener("input", update);
  list.addEventListener("change", update);
  list.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-role]");
    if (!button) return;
    const index = Number(button.closest(".pageOrderRow").dataset.index);
    const target = button.dataset.role === "up" ? index - 1 : index + 1;
    if (target < 0 || target >= pageOrderEntries.length) return;
    [pageOrderEntries[index], pageOrderEntries[target]] = [pageOrderEntries[target], pageOrderEntries[index]];
    writePageOrder();
    renderPageOrderEditor();
    list.querySelector(`.pageOrderRow[data-index="${target}"] button[data-role="${button.dataset.role}"]`)?.focus();
    updateDirtyState();
  });
}

// ---- Dirty state -------------------------------------------------------------

function featureDirty(kind) {
  return JSON.stringify(feature[kind]) !== feature.saved[kind];
}

function markSaved(kind) {
  feature.saved[kind] = JSON.stringify(feature[kind]);
}

function updateFeatureSaveState() {
  const pairs = [["texts", "textsSaveBtn", "textsState"], ["events", "eventsSaveBtn", "eventsState"], ["alarms", "alarmsSaveBtn", "alarmsState"]];
  for (const [kind, button, state] of pairs) {
    const dirtyNow = featureDirty(kind);
    $(button).disabled = !dirtyNow;
    $(state).textContent = feature.loaded ? tr(dirtyNow ? "Ungespeicherte Änderungen" : "Alles gespeichert") : "";
    $(state).classList.toggle("isDirty", dirtyNow);
  }
  const extrasDirty = featureDirty("extras");
  for (const button of document.querySelectorAll(".extrasSaveBtn")) button.disabled = !extrasDirty;
  for (const state of document.querySelectorAll(".extrasState")) {
    state.textContent = feature.loaded ? tr(extrasDirty ? "Ungespeicherte Änderungen" : "Alles gespeichert") : "";
    state.classList.toggle("isDirty", extrasDirty);
  }
  for (const [kind, page] of [["texts", "texts"], ["events", "events"], ["alarms", "alarms"]]) {
    document.querySelector(`.tabs a[data-page="${page}"]`)?.classList.toggle("hasFeatureChanges", featureDirty(kind));
  }
  document.querySelector('.tabs a[data-page="extras"]')?.classList.toggle("hasFeatureChanges", extrasDirty);
}

// ---- Scrolling texts -----------------------------------------------------------

function textSummary(text) {
  const parts = [];
  if (text.permanent) parts.push(tr("Dauerhaft"));
  if (text.from && text.to) parts.push(`${text.from}–${text.to}`);
  if (text.days !== 127) parts.push(dayOrder.filter((d) => text.days & (1 << d)).map((d) => dayNames()[d]).join(" "));
  if (text.date) parts.push(formatDateText(text.date));
  if (text.until) parts.push(`${tr("bis")} ${formatDateText(text.until)} ${text.until.slice(11, 16)}`);
  if (text.repeats > 1) parts.push(`${text.repeats}×`);
  return parts.join(" · ");
}

function renderTexts() {
  $("textsCount").textContent = `${feature.texts.length}/${feature.maxTexts}`;
  $("textAddBtn").disabled = feature.texts.length >= feature.maxTexts;
  if (!feature.texts.length) {
    $("textList").innerHTML = `<p class="emptyState">${escapeHtml(tr("Noch keine Lauftexte. Lege einen neuen an oder nimm ein Beispiel."))}</p>`;
    return;
  }
  $("textList").innerHTML = feature.texts.map((text, index) => `
    <div class="itemCard${text.enabled ? "" : " isOff"}" data-index="${index}">
      <label class="check itemToggle" title="${escapeHtml(tr("Aktiv"))}"><input type="checkbox" data-role="toggle" ${text.enabled ? "checked" : ""} aria-label="${escapeHtml(tr("Aktiv"))}"></label>
      <div class="itemMain">
        <strong class="textSample" style="color:${escapeHtml(text.rainbow ? "var(--accent)" : text.color)};${text.background && text.background !== "#000000" ? `background:${escapeHtml(text.background)};` : ""}">${escapeHtml(text.text)}</strong>
        <span class="itemMeta">${escapeHtml(textSummary(text) || tr("Immer im Seitenwechsel"))}</span>
      </div>
      <div class="itemActions">
        <button class="iconButton ghost small" type="button" data-role="up" aria-label="${escapeHtml(tr("Nach oben"))}" ${index === 0 ? "disabled" : ""}><svg class="icon" aria-hidden="true"><use href="#icon-up"></use></svg></button>
        <button class="iconButton ghost small" type="button" data-role="edit" aria-label="${escapeHtml(tr("Bearbeiten"))}"><svg class="icon" aria-hidden="true"><use href="#icon-edit"></use></svg></button>
        <button class="iconButton ghost small" type="button" data-role="delete" aria-label="${escapeHtml(tr("Löschen"))}"><svg class="icon" aria-hidden="true"><use href="#icon-trash"></use></svg></button>
      </div>
    </div>`).join("");
}

function newText(value = "") {
  return {
    enabled: true, text: value, color: $("colorText").value || "#fff5be", background: "#000000", speed: 5, direction: 0,
    font: 0, repeats: 1, pause: 0, duration: 0, permanent: false, rainbow: false, days: 127, from: "", to: "", date: "", until: ""
  };
}

function renderDayPicker(container, mask) {
  container.innerHTML = dayOrder.map((day) => `<label class="dayChip"><input type="checkbox" value="${day}" ${mask & (1 << day) ? "checked" : ""}><span>${dayNames()[day]}</span></label>`).join("");
}

function readDayPicker(container) {
  let mask = 0;
  for (const input of container.querySelectorAll("input:checked")) mask |= 1 << Number(input.value);
  return mask || 127;
}

let textDraft = null;

function openTextEditor(index, preset) {
  feature.editingText = index;
  textDraft = index >= 0 ? { ...feature.texts[index] } : newText(preset || "");
  $("textEditorTitle").lastChild.textContent = tr(index >= 0 ? "Lauftext bearbeiten" : "Neuer Lauftext");
  $("textValue").value = textDraft.text;
  $("textColor").value = textDraft.color || "#fff5be";
  $("textUseBackground").checked = Boolean(textDraft.background && textDraft.background !== "#000000");
  $("textBackground").value = $("textUseBackground").checked ? textDraft.background : "#202020";
  $("textRainbow").checked = Boolean(textDraft.rainbow);
  $("textEnabled").checked = textDraft.enabled !== false;
  $("textFont").value = String(textDraft.font || 0);
  $("textDirection").value = String(textDraft.direction || 0);
  $("textSpeed").value = String(textDraft.speed || 5);
  $("textSpeedValue").textContent = $("textSpeed").value;
  $("textRepeats").value = String(textDraft.repeats || 1);
  $("textPause").value = String(textDraft.pause || 0);
  $("textDuration").value = String(textDraft.duration || 0);
  renderDayPicker($("textDays"), textDraft.days || 127);
  $("textFrom").value = textDraft.from || "";
  $("textTo").value = textDraft.to || "";
  $("textDate").value = textDraft.date || "";
  $("textUntil").value = textDraft.until || "";
  $("textPermanent").checked = Boolean(textDraft.permanent);
  $("textScheduleDetails").open = Boolean(textDraft.from || textDraft.date || textDraft.until || textDraft.days !== 127);
  $("textEditor").hidden = false;
  $("textEditor").scrollIntoView({ behavior: "smooth", block: "start" });
  $("textValue").focus({ preventScroll: true });
}

function readTextEditor() {
  const clamp = (value, min, max) => Math.max(min, Math.min(max, Number(value) || 0));
  return {
    enabled: $("textEnabled").checked,
    text: $("textValue").value.trim(),
    color: $("textColor").value,
    background: $("textUseBackground").checked ? $("textBackground").value : "#000000",
    speed: clamp($("textSpeed").value, 1, 10),
    direction: Number($("textDirection").value),
    font: Number($("textFont").value),
    repeats: clamp($("textRepeats").value, 1, 20),
    pause: clamp($("textPause").value, 0, 30),
    duration: clamp($("textDuration").value, 0, 3600),
    permanent: $("textPermanent").checked,
    rainbow: $("textRainbow").checked,
    days: readDayPicker($("textDays")),
    from: $("textFrom").value && $("textTo").value ? $("textFrom").value : "",
    to: $("textFrom").value && $("textTo").value ? $("textTo").value : "",
    date: $("textDate").value,
    until: $("textUntil").value ? $("textUntil").value.slice(0, 16) : ""
  };
}

function closeTextEditor() {
  feature.editingText = -1;
  textDraft = null;
  $("textEditor").hidden = true;
}

function applyTextEditor() {
  const text = readTextEditor();
  if (!text.text) {
    message("Bitte einen Text eingeben.");
    $("textValue").focus();
    return;
  }
  if (feature.editingText >= 0) feature.texts[feature.editingText] = text;
  else feature.texts.push(text);
  closeTextEditor();
  renderTexts();
  updateFeatureSaveState();
}

async function testText() {
  const text = readTextEditor();
  if (!text.text) {
    message("Bitte einen Text eingeben.");
    return;
  }
  await featurePost("/api/texts/preview", { text });
  noteInteraction();
  scheduleLiveMatrix(200);
  message("Der Text läuft jetzt einmal über die Uhr.");
}

async function saveTexts() {
  const data = await featurePost("/api/texts", { texts: feature.texts });
  feature.texts = data.texts || [];
  markSaved("texts");
  renderTexts();
  updateFeatureSaveState();
  message("Lauftexte gespeichert.");
}

function initTexts() {
  $("textAddBtn").addEventListener("click", () => openTextEditor(-1));
  for (const button of document.querySelectorAll("[data-text-preset]")) {
    button.addEventListener("click", () => openTextEditor(-1, button.dataset.textPreset));
  }
  $("textList").addEventListener("click", (event) => {
    const button = event.target.closest("button[data-role]");
    if (!button) return;
    const index = Number(button.closest(".itemCard").dataset.index);
    if (button.dataset.role === "edit") return openTextEditor(index);
    if (button.dataset.role === "delete") {
      if (!confirm(tr("Diesen Lauftext löschen?"))) return;
      feature.texts.splice(index, 1);
      if (feature.editingText === index) closeTextEditor();
    }
    if (button.dataset.role === "up" && index > 0) {
      [feature.texts[index - 1], feature.texts[index]] = [feature.texts[index], feature.texts[index - 1]];
    }
    renderTexts();
    updateFeatureSaveState();
  });
  $("textList").addEventListener("change", (event) => {
    if (event.target.dataset.role !== "toggle") return;
    const index = Number(event.target.closest(".itemCard").dataset.index);
    feature.texts[index].enabled = event.target.checked;
    renderTexts();
    updateFeatureSaveState();
  });
  $("textSpeed").addEventListener("input", () => { $("textSpeedValue").textContent = $("textSpeed").value; });
  $("textApplyBtn").addEventListener("click", applyTextEditor);
  $("textCancelBtn").addEventListener("click", closeTextEditor);
  bindAction("textTestBtn", testText);
  bindSave("textsSaveBtn", saveTexts);
}

// ---- Calendar ------------------------------------------------------------------

const eventPresets = {
  christmas: () => ({ type: 3, name: currentLanguage === "de" ? "Weihnachten" : "Christmas", date: `${deviceToday().getFullYear()}-12-24`, repeat: 1, lead: 30, color: "#ff3030" }),
  newyear: () => ({ type: 3, name: currentLanguage === "de" ? "Silvester" : "New Year's Eve", date: `${deviceToday().getFullYear()}-12-31`, repeat: 1, lead: 7, color: "#ffc000" }),
  birthday: () => ({ type: 1, name: "", date: "", repeat: 1, lead: 7, color: "#ff50a0" }),
  waste: () => ({ type: 2, name: currentLanguage === "de" ? "Gelbe Tonne" : "Recycling bin", date: isoDate(deviceToday()), repeat: 3, interval: 2, lead: 1, color: "#ffd000" }),
  vacation: () => ({ type: 4, name: currentLanguage === "de" ? "Urlaub" : "Vacation", date: "", repeat: 0, lead: 60, color: "#2090ff" }),
  appointment: () => ({ type: 0, name: "", date: isoDate(deviceToday()), repeat: 0, lead: 3, color: "#50b4ff" })
};

function newEvent(preset = {}) {
  return { enabled: true, show: true, name: "", description: "", text: "", date: "", time: "", type: 0, repeat: 0, interval: 1, lead: 7, color: "#50b4ff", ...preset };
}

function eventWhen(event) {
  const next = nextOccurrence(event, deviceToday());
  if (!next) return tr("vorbei");
  const days = daysBetween(deviceToday(), next);
  const when = days === 0 ? tr("heute") : days === 1 ? tr("morgen") : trFormat("in {days} Tagen", { days });
  return `${formatDateText(next, true)} · ${when}`;
}

function eventRepeatText(event) {
  const repeat = Number(event.repeat) || 0;
  if ((repeat === 2 || repeat === 3) && Number(event.interval) > 1) {
    return trFormat(repeat === 3 ? "alle {n} Wochen" : "alle {n} Monate", { n: event.interval });
  }
  return tr(repeatNames[repeat]);
}

function renderEvents() {
  $("eventsCount").textContent = `${feature.events.length}/${feature.maxEvents}`;
  $("eventAddBtn").disabled = feature.events.length >= feature.maxEvents;
  const sorted = feature.events.map((event, index) => ({ event, index, next: nextOccurrence(event, deviceToday()) }))
    .sort((a, b) => (a.next ? a.next.getTime() : Infinity) - (b.next ? b.next.getTime() : Infinity));
  if (!sorted.length) {
    $("eventList").innerHTML = `<p class="emptyState">${escapeHtml(tr("Noch keine Termine. Nimm eine Vorlage oder lege einen neuen an."))}</p>`;
    return;
  }
  $("eventList").innerHTML = sorted.map(({ event, index }) => `
    <div class="itemCard${event.enabled ? "" : " isOff"}" data-index="${index}">
      <span class="itemIcon" style="color:${escapeHtml(event.color)}"><svg class="icon" aria-hidden="true"><use href="#${eventTypeIcons[event.type] || "icon-calendar"}"></use></svg></span>
      <div class="itemMain">
        <strong>${escapeHtml(event.name)}${event.time ? ` <span class="itemTime">${escapeHtml(event.time)}</span>` : ""}</strong>
        <span class="itemMeta">${escapeHtml(tr(eventTypeNames[event.type] || "Termin"))} · ${escapeHtml(eventRepeatText(event))} · ${escapeHtml(eventWhen(event))}</span>
        <span class="itemMeta">${escapeHtml(!event.show ? tr("Nur im Kalender") : Number(event.lead) === 0 ? tr("Auf der Uhr nur am Tag selbst")
          : Number(event.lead) === 1 ? tr("Auf der Uhr ab 1 Tag vorher") : trFormat("Auf der Uhr ab {days} Tage vorher", { days: event.lead }))}</span>
      </div>
      <div class="itemActions">
        <label class="check itemToggle"><input type="checkbox" data-role="toggle" ${event.enabled ? "checked" : ""} aria-label="${escapeHtml(tr("Aktiv"))}"></label>
        <button class="iconButton ghost small" type="button" data-role="edit" aria-label="${escapeHtml(tr("Bearbeiten"))}"><svg class="icon" aria-hidden="true"><use href="#icon-edit"></use></svg></button>
        <button class="iconButton ghost small" type="button" data-role="delete" aria-label="${escapeHtml(tr("Löschen"))}"><svg class="icon" aria-hidden="true"><use href="#icon-trash"></use></svg></button>
      </div>
    </div>`).join("");
}

function renderUpcoming() {
  const html = feature.upcoming.length
    ? feature.upcoming.map((item) => `<div class="upcomingItem"><span class="upcomingDays">${escapeHtml(item.days === 0 ? tr("Heute") : item.days === 1 ? tr("Morgen") : trFormat("in {days} T.", { days: item.days }))}</span><span>${escapeHtml(item.text)}</span><span class="itemMeta">${escapeHtml(formatDateText(item.date))}</span></div>`).join("")
    : `<p class="emptyState">${escapeHtml(tr("In den nächsten 60 Tagen steht nichts an."))}</p>`;
  $("upcomingList").innerHTML = html;
  const soon = feature.upcoming.filter((item) => item.days <= 7).slice(0, 4);
  $("overviewUpcoming").hidden = !soon.length;
  $("overviewUpcomingList").innerHTML = soon.map((item) => `<div class="upcomingItem"><span class="upcomingDays">${escapeHtml(item.days === 0 ? tr("Heute") : item.days === 1 ? tr("Morgen") : trFormat("in {days} T.", { days: item.days }))}</span><span>${escapeHtml(item.text)}</span></div>`).join("");
  $("holidayList").innerHTML = feature.holidays.map((day) => `<div class="holidayItem${day.legal ? "" : " isObservance"}"><span>${escapeHtml(formatDateText(day.date, true))}</span><span>${escapeHtml(day.name)}</span></div>`).join("");
}

let eventDraft = null;

function updateEventEditorFields() {
  const type = Number($("eventType").value);
  const repeat = Number($("eventRepeat").value);
  $("eventIntervalField").hidden = repeat !== 2 && repeat !== 3;
  $("eventDateLabel").textContent = tr(type === 1 ? "Geburtsdatum (mit Jahr für das Alter)" : "Datum");
  const draft = readEventEditor();
  const next = nextOccurrence(draft, deviceToday());
  $("eventPreview").textContent = next
    ? `${tr("Nächstes Mal")}: ${formatDateText(next, true)} (${(() => {
      const days = daysBetween(deviceToday(), next);
      return days === 0 ? tr("heute") : days === 1 ? tr("morgen") : trFormat("in {days} Tagen", { days });
    })()})`
    : draft.date ? tr("Dieser Termin liegt in der Vergangenheit.") : "";
}

function openEventEditor(index, preset) {
  feature.editingEvent = index;
  eventDraft = index >= 0 ? { ...feature.events[index] } : newEvent(preset);
  $("eventEditorTitle").lastChild.textContent = tr(index >= 0 ? "Termin bearbeiten" : "Neuer Termin");
  $("eventType").value = String(eventDraft.type);
  $("eventName").value = eventDraft.name;
  $("eventDate").value = eventDraft.date;
  $("eventTime").value = eventDraft.time;
  $("eventRepeat").value = String(eventDraft.repeat);
  $("eventInterval").value = String(eventDraft.interval || 1);
  $("eventLead").value = String(eventDraft.lead ?? 7);
  $("eventColor").value = eventDraft.color || "#50b4ff";
  $("eventDescription").value = eventDraft.description || "";
  $("eventText").value = eventDraft.text || "";
  $("eventShow").checked = eventDraft.show !== false;
  $("eventEnabled").checked = eventDraft.enabled !== false;
  updateEventEditorFields();
  $("eventEditor").hidden = false;
  $("eventEditor").scrollIntoView({ behavior: "smooth", block: "start" });
  (eventDraft.name ? $("eventDate") : $("eventName")).focus({ preventScroll: true });
}

function readEventEditor() {
  const clamp = (value, min, max) => Math.max(min, Math.min(max, Number(value) || 0));
  return {
    enabled: $("eventEnabled").checked,
    show: $("eventShow").checked,
    name: $("eventName").value.trim(),
    description: $("eventDescription").value.trim(),
    text: $("eventText").value.trim(),
    date: $("eventDate").value,
    time: $("eventTime").value,
    type: Number($("eventType").value),
    repeat: Number($("eventRepeat").value),
    interval: clamp($("eventInterval").value, 1, 12),
    lead: clamp($("eventLead").value, 0, 366),
    color: $("eventColor").value
  };
}

function closeEventEditor() {
  feature.editingEvent = -1;
  eventDraft = null;
  $("eventEditor").hidden = true;
}

function applyEventEditor() {
  const event = readEventEditor();
  if (!event.name) {
    message("Bitte einen Namen eingeben.");
    $("eventName").focus();
    return;
  }
  if (!event.date) {
    message("Bitte ein Datum auswählen.");
    $("eventDate").focus();
    return;
  }
  if (feature.editingEvent >= 0) feature.events[feature.editingEvent] = event;
  else feature.events.push(event);
  closeEventEditor();
  renderEvents();
  updateFeatureSaveState();
}

async function loadEvents() {
  const data = await featureFetch("/api/events");
  feature.events = data.events || [];
  feature.upcoming = data.upcoming || [];
  feature.holidays = data.holidays || [];
  feature.maxEvents = data.max || feature.maxEvents;
  feature.eventsFetchedAt = Date.now();
  markSaved("events");
}

async function saveEvents() {
  const data = await featurePost("/api/events", { events: feature.events });
  feature.events = data.events || [];
  feature.upcoming = data.upcoming || [];
  feature.holidays = data.holidays || [];
  markSaved("events");
  renderEvents();
  renderUpcoming();
  updateFeatureSaveState();
  message("Termine gespeichert.");
}

function initEvents() {
  $("eventAddBtn").addEventListener("click", () => openEventEditor(-1));
  for (const button of document.querySelectorAll("[data-event-preset]")) {
    button.addEventListener("click", () => openEventEditor(-1, eventPresets[button.dataset.eventPreset]()));
  }
  $("eventList").addEventListener("click", (event) => {
    const button = event.target.closest("button[data-role]");
    if (!button) return;
    const index = Number(button.closest(".itemCard").dataset.index);
    if (button.dataset.role === "edit") return openEventEditor(index);
    if (button.dataset.role === "delete") {
      if (!confirm(tr("Diesen Termin löschen?"))) return;
      feature.events.splice(index, 1);
      if (feature.editingEvent === index) closeEventEditor();
      renderEvents();
      updateFeatureSaveState();
    }
  });
  $("eventList").addEventListener("change", (event) => {
    if (event.target.dataset.role !== "toggle") return;
    feature.events[Number(event.target.closest(".itemCard").dataset.index)].enabled = event.target.checked;
    renderEvents();
    updateFeatureSaveState();
  });
  for (const id of ["eventType", "eventRepeat", "eventDate", "eventInterval"]) {
    $(id).addEventListener("input", updateEventEditorFields);
    $(id).addEventListener("change", updateEventEditorFields);
  }
  $("eventType").addEventListener("change", () => {
    if (feature.editingEvent >= 0) return;
    const type = Number($("eventType").value);
    const defaults = { 1: [1, 7], 2: [3, 1], 3: [0, 30], 4: [0, 60], 5: [0, 1], 0: [0, 3] }[type];
    $("eventRepeat").value = String(defaults[0]);
    $("eventLead").value = String(defaults[1]);
    updateEventEditorFields();
  });
  $("eventApplyBtn").addEventListener("click", applyEventEditor);
  $("eventCancelBtn").addEventListener("click", closeEventEditor);
  bindSave("eventsSaveBtn", saveEvents);
}

// ---- Alarms ----------------------------------------------------------------------

function renderAlarms() {
  $("alarmsCount").textContent = `${feature.alarms.length}/${feature.maxAlarms}`;
  $("alarmAddBtn").disabled = feature.alarms.length >= feature.maxAlarms;
  if (!feature.alarms.length) {
    $("alarmList").innerHTML = `<p class="emptyState">${escapeHtml(tr("Noch kein Wecker gestellt."))}</p>`;
    return;
  }
  $("alarmList").innerHTML = feature.alarms.map((alarm, index) => `
    <div class="alarmCard${alarm.enabled ? "" : " isOff"}" data-index="${index}">
      <div class="alarmTop">
        <input class="alarmTime" type="time" data-field="time" value="${escapeHtml(alarm.time)}" aria-label="${escapeHtml(tr("Weckzeit"))}">
        <input class="alarmLabel" data-field="label" maxlength="40" value="${escapeHtml(alarm.label)}" placeholder="${escapeHtml(tr("Bezeichnung (optional)"))}">
        <label class="check itemToggle"><input type="checkbox" data-field="enabled" ${alarm.enabled ? "checked" : ""} aria-label="${escapeHtml(tr("Aktiv"))}"></label>
        <button class="iconButton ghost small" type="button" data-role="delete" aria-label="${escapeHtml(tr("Löschen"))}"><svg class="icon" aria-hidden="true"><use href="#icon-trash"></use></svg></button>
      </div>
      <div class="dayPicker">${dayOrder.map((day) => `<label class="dayChip"><input type="checkbox" data-day="${day}" ${alarm.days & (1 << day) ? "checked" : ""}><span>${dayNames()[day]}</span></label>`).join("")}</div>
      <div class="alarmOptions">
        <label class="inlineCheck"><input type="checkbox" data-field="buzzer" ${alarm.buzzer ? "checked" : ""}>${escapeHtml(tr("Summer"))}</label>
        <label class="inlineCheck"><input type="checkbox" data-field="flash" ${alarm.flash ? "checked" : ""}>${escapeHtml(tr("LED blinkt"))}</label>
        <label class="inlineNumber">${escapeHtml(tr("Dauer"))} <input type="number" min="1" max="30" data-field="duration" value="${alarm.duration}"> min</label>
        <label class="inlineNumber">${escapeHtml(tr("Schlummern"))} <input type="number" min="1" max="30" data-field="snooze" value="${alarm.snooze}"> min</label>
      </div>
    </div>`).join("");
}

function initAlarms() {
  $("alarmAddBtn").addEventListener("click", () => {
    feature.alarms.push({ enabled: true, time: "07:00", days: 62, label: "", buzzer: true, flash: true, duration: 5, snooze: 9 });
    renderAlarms();
    updateFeatureSaveState();
    $("alarmList").querySelector(".alarmCard:last-child .alarmTime")?.focus();
  });
  const update = (event) => {
    const card = event.target.closest(".alarmCard");
    if (!card) return;
    const alarm = feature.alarms[Number(card.dataset.index)];
    const target = event.target;
    if (target.dataset.day !== undefined) {
      const bit = 1 << Number(target.dataset.day);
      alarm.days = target.checked ? alarm.days | bit : alarm.days & ~bit;
      if (!alarm.days) alarm.days = 127;
    } else if (target.dataset.field) {
      const field = target.dataset.field;
      if (target.type === "checkbox") alarm[field] = target.checked;
      else if (target.type === "number") alarm[field] = Math.max(1, Math.min(30, Number(target.value) || 1));
      else alarm[field] = target.value;
      if (field === "enabled") card.classList.toggle("isOff", !target.checked);
    }
    updateFeatureSaveState();
  };
  $("alarmList").addEventListener("input", update);
  $("alarmList").addEventListener("change", update);
  $("alarmList").addEventListener("click", (event) => {
    const button = event.target.closest('button[data-role="delete"]');
    if (!button) return;
    feature.alarms.splice(Number(button.closest(".alarmCard").dataset.index), 1);
    renderAlarms();
    updateFeatureSaveState();
  });
  bindSave("alarmsSaveBtn", async () => {
    if (feature.alarms.some((alarm) => !/^\d{2}:\d{2}$/.test(alarm.time))) {
      message("Bitte für jeden Wecker eine Uhrzeit eingeben.");
      return;
    }
    const data = await featurePost("/api/alarms", { alarms: feature.alarms });
    feature.alarms = data.alarms || [];
    markSaved("alarms");
    renderAlarms();
    updateFeatureSaveState();
    message("Wecker gespeichert.");
  });
  bindAction("alarmBuzzerTestBtn", () => $("buzzerTestBtn").click());
  for (const button of document.querySelectorAll(".alarmStopBtn")) {
    button.addEventListener("click", async () => {
      await apiFetch("/api/alarm/stop", { method: "POST" }).catch(() => {});
      for (const banner of document.querySelectorAll(".alarmBanner")) banner.hidden = true;
    });
  }
  for (const button of document.querySelectorAll(".alarmSnoozeBtn")) {
    button.addEventListener("click", async () => {
      await apiFetch("/api/alarm/snooze", { method: "POST" }).catch(() => {});
      for (const banner of document.querySelectorAll(".alarmBanner")) banner.hidden = true;
      message("Der Wecker klingelt gleich noch einmal.");
    });
  }
}

// ---- Extras ------------------------------------------------------------------------

function setExtrasForm() {
  for (const el of document.querySelectorAll("[data-extra]")) {
    const value = feature.extras[el.dataset.extra];
    if (el.type === "checkbox") el.checked = Boolean(value);
    else el.value = value ?? "";
  }
  $("exUseMessageColor").checked = Boolean(feature.extras.messageColor);
  $("exMessageColor").value = feature.extras.messageColor || $("colorText").value || "#fff5be";
  $("exMessageSpeedValue").textContent = $("exMessageSpeed").value;
}

function readExtrasField(el) {
  const key = el.dataset.extra;
  if (el.type === "checkbox") feature.extras[key] = el.checked;
  else if (el.type === "number" || el.type === "range" || key === "animation") feature.extras[key] = Number(el.value);
  else feature.extras[key] = el.value;
}

function renderNews() {
  const news = feature.news || {};
  const items = (news.items || []).map((item) => `<li>${escapeHtml(item)}</li>`).join("");
  $("newsList").innerHTML = news.error
    ? `<p class="inlineError">${escapeHtml(news.error)}</p>`
    : items ? `<ol class="bulletList">${items}</ol>` : feature.extras.newsUrl ? `<p class="hint">${escapeHtml(tr("Noch keine Schlagzeilen geladen."))}</p>` : "";
}

async function saveExtras() {
  const data = await featurePost("/api/extras", { extras: feature.extras });
  feature.extras = data.extras || feature.extras;
  feature.news = data.news || feature.news;
  markSaved("extras");
  setExtrasForm();
  renderNews();
  updateFeatureSaveState();
  message("Gespeichert.");
  loadEvents().then(renderUpcoming).catch(() => {});
}

function initExtras() {
  const onInput = (event) => {
    const el = event.target;
    if (el.dataset.extra) readExtrasField(el);
    if (el.id === "exUseMessageColor" || el.id === "exMessageColor") {
      feature.extras.messageColor = $("exUseMessageColor").checked ? $("exMessageColor").value : "";
    }
    if (el.id === "exMessageSpeed") $("exMessageSpeedValue").textContent = el.value;
    updateFeatureSaveState();
  };
  for (const page of ["events", "alarms", "extras"]) {
    const root = document.querySelector(`.page[data-page="${page}"]`);
    root.addEventListener("input", (event) => { if (event.target.dataset.extra || event.target.id?.startsWith("exUse") || event.target.id === "exMessageColor") onInput(event); });
    root.addEventListener("change", (event) => { if (event.target.dataset.extra || event.target.id?.startsWith("exUse") || event.target.id === "exMessageColor") onInput(event); });
  }
  for (const button of document.querySelectorAll(".extrasSaveBtn")) {
    button.addEventListener("click", async () => {
      button.disabled = true;
      try { await saveExtras(); } catch (error) { message(error.message); updateFeatureSaveState(); }
    });
  }
  bindAction("newsRefreshBtn", async () => {
    if (featureDirty("extras")) await saveExtras();
    const res = await apiFetch("/api/news/refresh", { method: "POST" });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      message(data.error || "Aktion fehlgeschlagen.");
      return;
    }
    message("Schlagzeilen werden geladen...");
    setTimeout(async () => {
      const extrasData = await featureFetch("/api/extras").catch(() => null);
      if (extrasData) {
        feature.news = extrasData.news || feature.news;
        renderNews();
      }
    }, 8000);
  });
}

// ---- Loading, status, export ---------------------------------------------------

async function loadFeatureData() {
  if (!capabilities.pixeluhrFeatures) return;
  const texts = await featureFetch("/api/texts");
  feature.texts = texts.texts || [];
  feature.maxTexts = texts.max || feature.maxTexts;
  markSaved("texts");
  await loadEvents();
  const alarms = await featureFetch("/api/alarms");
  feature.alarms = alarms.alarms || [];
  feature.maxAlarms = alarms.max || feature.maxAlarms;
  markSaved("alarms");
  const extras = await featureFetch("/api/extras");
  feature.extras = extras.extras || {};
  feature.news = extras.news || feature.news;
  markSaved("extras");
  feature.loaded = true;
  refreshFeatureLanguage();
}

function refreshFeatureLanguage() {
  if (!$("pageOrderEditor")) return;
  if (savedConfig) renderPageOrderEditor();
  if (!feature.loaded) return;
  renderTexts();
  renderEvents();
  renderUpcoming();
  renderAlarms();
  setExtrasForm();
  renderNews();
  updateFeatureSaveState();
  if (textDraft) renderDayPicker($("textDays"), readDayPicker($("textDays")));
  const buzzer = savedConfig?.buzzerPin;
  $("alarmBuzzerHint").textContent = buzzer ? trFormat("Summer an GPIO {pin}. Ändern unter Hardware.", { pin: buzzer }) : tr("Kein Summer eingestellt. Unter Hardware festlegen.");
}

function updateFeatureStatus(status) {
  for (const banner of document.querySelectorAll(".alarmBanner")) banner.hidden = !status.alarmRinging;
  if (typeof updateNightModeControls === "function" && $("nightMode")) updateNightModeControls();
  if (feature.loaded && Date.now() - feature.eventsFetchedAt > 10 * 60 * 1000 && !featureDirty("events")) {
    feature.eventsFetchedAt = Date.now();
    loadEvents().then(() => { renderEvents(); renderUpcoming(); }).catch(() => {});
  }
}

async function exportFeatureData() {
  if (!capabilities.pixeluhrFeatures) return undefined;
  const [texts, events, alarms, extras] = [await featureFetch("/api/texts"), await featureFetch("/api/events"),
    await featureFetch("/api/alarms"), await featureFetch("/api/extras")];
  return { texts: texts.texts, events: events.events, alarms: alarms.alarms, extras: extras.extras };
}

async function importFeatureData(data) {
  if (Array.isArray(data.texts)) await featurePost("/api/texts", { texts: data.texts });
  if (Array.isArray(data.events)) await featurePost("/api/events", { events: data.events });
  if (Array.isArray(data.alarms)) await featurePost("/api/alarms", { alarms: data.alarms });
  if (data.extras && typeof data.extras === "object") await featurePost("/api/extras", { extras: data.extras });
  await loadFeatureData();
  message("Lauftexte, Termine, Wecker und Extras übernommen.");
}

// Like bindAction(), but leaves the button state to updateFeatureSaveState().
function bindSave(id, action) {
  $(id).addEventListener("click", async () => {
    $(id).disabled = true;
    try { await action(); } catch (error) {
      if (error.message !== "Unauthorized") message(error.message === "Failed to fetch" ? "Uhr nicht erreichbar" : error.message);
    } finally { updateFeatureSaveState(); }
  });
}

function initFeatures() {
  initPageOrderEditor();
  initTexts();
  initEvents();
  initAlarms();
  initExtras();
  fillSelectedPageOptions();
  for (const id of ["pageOrderEditor", "textList", "eventList", "alarmList", "upcomingList", "overviewUpcomingList", "holidayList", "newsList", "textDays"]) {
    $(id).dataset.noI18n = "";
  }
  window.addEventListener("beforeunload", (event) => {
    if (feature.loaded && ["texts", "events", "alarms", "extras"].some(featureDirty)) {
      event.preventDefault();
      event.returnValue = "";
    }
  });
}

initFeatures();
// The config may have arrived before this script ran.
if (savedConfig) renderPageOrderEditor();
if (savedConfig && capabilities.pixeluhrFeatures && !feature.loaded) loadFeatureData().catch(() => {});
