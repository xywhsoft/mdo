/* This function is deliberately self-contained: native hosts embed its source
 * in a browser execution context. Page data never grants host authority. */
export function browserRuntime(configuration = {}) {
  const version = 20;
  const localFrame = configuration.local_frame === true;
  const marker = localFrame ? "__mdo_browser_use_local_frame_v1" : "__mdo_browser_use_runtime_v1";
  const previous = Object.getOwnPropertyDescriptor(globalThis, marker)?.value;
  if (previous?.version === version && previous.documentNode === document) return previous;
  // randomUUID is secure-context-only. getRandomValues also supports ordinary
  // HTTP documents, while retaining a full random v4 document identity.
  const identityBytes=new Uint8Array(16);
  globalThis.crypto.getRandomValues(identityBytes);
  identityBytes[6]=(identityBytes[6]&15)|64;identityBytes[8]=(identityBytes[8]&63)|128;
  const identityHex=Array.from(identityBytes,byte=>byte.toString(16).padStart(2,'0')).join('');
  const documentId=`d-${identityHex.slice(0,8)}-${identityHex.slice(8,12)}-${identityHex.slice(12,16)}-${identityHex.slice(16,20)}-${identityHex.slice(20)}`;
  const refs = new Map(), identities = new WeakMap(), frameIds = new WeakMap();
  let nextRef = 1, nextFrame = 1;
  const maxRefs = 2000, maxFrames = 32;
  const normalize = text => String(text || "").replace(/\s+/g, " ").trim();
  const fail = (code, message, details = {}) => ({ ok: false, error: { code, message, ...details } });
  function clip(text, maximum) {
    const bytes = new TextEncoder().encode(text);
    if (bytes.length <= maximum) return { text, truncated: false };
    let end = maximum;
    while (end > 0) {
      try { return { text: new TextDecoder("utf-8", { fatal: true }).decode(bytes.subarray(0, end)), truncated: true }; }
      catch { end--; }
    }
    return { text: "", truncated: true };
  }
  function visible(element) {
    if (!element.isConnected || element.closest("[hidden],[inert],[aria-hidden='true']")) return false;
    const style = element.ownerDocument.defaultView.getComputedStyle(element);
    return style.display !== "none" && style.visibility !== "hidden" && style.visibility !== "collapse"
      && Number(style.opacity) !== 0 && element.getClientRects().length > 0;
  }
  function role(element) {
    const explicit = normalize(element.getAttribute("role")).split(" ")[0];
    if (explicit) return explicit;
    const tag = element.localName;
    if (tag === "a" && element.hasAttribute("href")) return "link";
    if (tag === "button" || tag === "summary") return "button";
    if (tag === "textarea" || element.isContentEditable) return "textbox";
    if (tag === "select") return element.multiple ? "listbox" : "combobox";
    if (tag === "option") return "option";
    if (tag === "input") {
      const type = element.type;
      if (type === "hidden") return "";
      if (["checkbox", "radio"].includes(type)) return type;
      if (["submit", "button", "reset", "image"].includes(type)) return "button";
      if (type === "range") return "slider";
      if (type === "number") return "spinbutton";
      if (type === "file") return "fileinput";
      return "textbox";
    }
    if (/^h[1-6]$/.test(tag)) return "heading";
    return "";
  }
  const labelExcluded=new Set(['input','textarea','select','option','button','meter','output','progress','script','style','noscript','template']);
  function editableText(element) {
    let node=element;
    for(let depth=0;node&&depth<256;depth++){
      if(node.isContentEditable)return true;
      node=node.assignedSlot||node.parentElement||node.getRootNode()?.host;
    }
    return Boolean(node); // An unbounded ancestry must not expose editor text.
  }
  function labelText(element) {
    if(!element)return '';
    if(editableText(element))return '';
    if(['input','textarea','select','option','meter','output','progress'].includes(element.localName))return '';
    const walker=element.ownerDocument.createTreeWalker(element,NodeFilter.SHOW_ELEMENT|NodeFilter.SHOW_TEXT,
      {acceptNode:node=>node.nodeType===1&&(labelExcluded.has(node.localName)||node.isContentEditable)?NodeFilter.FILTER_REJECT:NodeFilter.FILTER_ACCEPT});
    let node,text='',visited=0;
    while((node=walker.nextNode())&&visited++<512&&text.length<4096)if(node.nodeType===3)text+=node.nodeValue.slice(0,4096-text.length);
    return normalize(text);
  }
  function name(element) {
    const labelled = normalize(element.getAttribute("aria-labelledby"));
    if (labelled) {
      const root = element.getRootNode();
      const text = labelled.split(" ").slice(0, 16)
        .map(id => labelText(root.getElementById?.(id))).join(" ");
      if (normalize(text)) return normalize(text).slice(0, 512);
    }
    const aria = element.getAttribute("aria-label");
    if (aria) return normalize(aria).slice(0, 512);
    if(element.localName==='option')return normalize(element.label).slice(0,512);
    if (element.labels?.length) return normalize([...element.labels].map(labelText).join(" ")).slice(0, 512);
    if (element.localName === "input") {
      if (["submit", "reset", "button"].includes(element.type)) return normalize(element.value).slice(0, 512);
      return normalize(element.getAttribute("placeholder") || element.getAttribute("title") || "").slice(0, 512);
    }
    if(['select','textarea'].includes(element.localName)||element.isContentEditable)return normalize(element.getAttribute('aria-placeholder')||element.getAttribute('placeholder')||element.getAttribute('title')||'').slice(0,512);
    return normalize(element.getAttribute("alt") || labelText(element) || element.getAttribute("title") || "").slice(0, 512);
  }
  function reference(element, frame) {
    let id = identities.get(element);
    if (id && refs.has(id)) return id;
    if (refs.size >= maxRefs) {
      for (const [key, row] of refs) if (!row.element.isConnected) refs.delete(key);
      if (refs.size >= maxRefs) return null;
    }
    id = `e${nextRef++}`;
    refs.set(id, { element, frame, documentNode: element.ownerDocument });
    identities.set(element, id);
    return id;
  }
  function frameIdentity(node) {
    let id = frameIds.get(node);
    if (!id) { id = `f${nextFrame++}`; frameIds.set(node, id); }
    return id;
  }
  function inspect(element, frame) {
    const rect = element.getBoundingClientRect();
    const item = {
      ref: reference(element, frame), frame, role: role(element), name: name(element),
      disabled: element.matches(":disabled") || element.getAttribute("aria-disabled") === "true",
      editable: (element.isContentEditable || ["input", "textarea"].includes(element.localName))
        && !element.readOnly && !element.disabled,
      bounds: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
      coordinate_space: "frame-css"
    };
    if (element.localName === "input") {
      item.input_type = element.type;
      if (element.type === "file") {
        item.editable = false;
        item.multiple = element.multiple; item.accept = clip(element.accept || "", 1024).text;
        item.directory = Boolean(element.webkitdirectory); item.hidden = !visible(element);
        item.selected_count = element.files?.length || 0;
      }
    }
    if (element.matches("input[type=checkbox],input[type=radio]")) item.checked = element.checked;
    if (element.localName === "select") { item.multiple = element.multiple; item.option_count = element.options.length; }
    if (element.localName === "a") {
      try { const url = new URL(element.href); url.username = ""; url.password = ""; item.url = url.href.slice(0, 2048); }
      catch { /* Invalid links are still locatable, but have no usable URL. */ }
    }
    // Input values are intentionally omitted, including password field values.
    return item;
  }
  function scan(options = {}) {
    const limit = localFrame && options.limit === 0 ? 0 : Math.min(400, Math.max(1, Number(options.limit) || 200));
    const nodeLimit = Math.min(6000, Math.max(1, Number(options._max_nodes) || 6000));
    const frames = [{ id: "f0", url: location.href, parent: null, dom: true }];
    const elements = [], text = [];
    let visited = 0, incomplete = false;
    const started = performance.now();
    function walk(root, frame, includeText) {
      const doc = root.ownerDocument || root;
      const walker = doc.createTreeWalker(root, NodeFilter.SHOW_ELEMENT);
      let element;
      while ((element = walker.nextNode())) {
        if (visited >= nodeLimit || performance.now() - started > 100) { incomplete = true; return; }
        visited++;
        if (["script", "style", "noscript", "template"].includes(element.localName)) continue;
        const shown = visible(element);
        if (shown && includeText && !element.children.length && !editableText(element) && !["input", "textarea", "select", "option"].includes(element.localName)) {
          const value = normalize(element.textContent); if (value) text.push(value.slice(0, 2048));
        }
        const fileInput = element.localName === "input" && element.type === "file";
        const elementRole = shown || fileInput ? role(element) : "";
        const matched = typeof options._match === "function" ? (shown || fileInput) && options._match(element, frame) : Boolean(elementRole);
        if (matched && elements.length < limit) {
          const item = inspect(element, frame);
          if (item.ref) elements.push(item); else incomplete = true;
        } else if (matched) incomplete = true;
        if ((shown || options._upload) && element.shadowRoot) walk(element.shadowRoot, frame, includeText);
        if (!localFrame && shown && ["iframe", "frame"].includes(element.localName)) {
          if (frames.length >= maxFrames) { incomplete = true; continue; }
          const id = frameIdentity(element);
          let child = null;
          try { child = element.contentDocument; } catch { /* Cross-origin frame. */ }
          const entry = { id, parent: frame, url: element.getAttribute("src") || "about:srcdoc", dom: Boolean(child) };
          frames.push(entry);
          if (child) walk(child, id, includeText);
          else entry.reason = "cross_origin_or_unavailable";
        }
      }
    }
    // Locator scans need matching metadata, not a second readable-page copy.
    // Text clipping must not make an otherwise complete exact search fail.
    walk(document, "f0", typeof options._match !== "function" && options._include_text !== false);
    const readable = clip(text.join("\n"), Math.min(32768, Math.max(256, Number(options.max_bytes) || 16384)));
    return {
      ...describe(), text: readable.text, elements, frames,
      truncated: incomplete || readable.truncated, visited,
      semantics: "dom-subset-v1"
    };
  }
  function describe() {
    return {ok:true,document:documentId,url:location.href,title:document.title,ready:document.readyState,
      viewport:{width:innerWidth,height:innerHeight,scroll_x:scrollX,scroll_y:scrollY,
        scale:visualViewport?.scale||1,device_pixel_ratio:devicePixelRatio}};
  }
  function locate(options = {}) {
    if (options.document !== documentId) return fail("stale_document", "Observe the current document before acting");
    const target = options.target || {};
    let candidates = [], visited = 0;
    const measured = result => localFrame ? {...result,visited} : result;
    if (target.ref) {
      const row = refs.get(target.ref);
      if (!row || !row.element.isConnected || row.element.ownerDocument !== row.documentNode) {
        return fail("element_detached", "The referenced element is no longer present");
      }
      try {
        const view = row.documentNode.defaultView;
        if (!view || view.document !== row.documentNode ||
            (row.frame !== "f0" && view.frameElement?.contentDocument !== row.documentNode)) {
          return fail("stale_frame", "The referenced frame has navigated; observe it again");
        }
      } catch { return fail("stale_frame", "The referenced frame is no longer accessible"); }
      candidates = [row];
    } else {
      let snapshot;
      try { snapshot = scan({ limit: 400, _match: (element, frame) => {
        if (options._upload && !(element.localName === "input" && element.type === "file")) return false;
        if (target.frame && frame !== target.frame) return false;
        if (target.name !== undefined && !role(element)) return false;
        if (target.role && role(element) !== target.role) return false;
        if (target.name !== undefined && name(element) !== target.name) return false;
        if (target.label !== undefined && normalize([...element.labels || []].map(labelText).join(" ")) !== target.label) return false;
        if (target.placeholder !== undefined && element.getAttribute("placeholder") !== target.placeholder) return false;
        if (target.testid !== undefined && element.getAttribute("data-testid") !== target.testid) return false;
        if (target.css && !element.matches(target.css)) return false;
        if (target.text !== undefined && (normalize(element.textContent) !== target.text ||
          [...element.children].some(child => normalize(child.textContent) === target.text))) return false;
        return ["role", "name", "label", "placeholder", "testid", "css", "text"].some(key => target[key] !== undefined);
      }, _upload: Boolean(options._upload), _max_nodes: options._max_nodes }); } catch { return fail("locator_invalid", "The target contains an invalid CSS selector"); }
      visited = snapshot.visited;
      if (snapshot.truncated) return measured(fail("observation_incomplete", "Narrow the search in a fresh observation"));
      candidates = snapshot.elements.map(item => refs.get(item.ref));
    }
    candidates = candidates.filter(row => visible(row.element) || options._upload && row.element.localName === "input" && row.element.type === "file");
    if (!candidates.length) return measured(fail("element_not_found", "No visible element matches the target"));
    if (candidates.length > 1) return measured(fail("element_ambiguous", "More than one element matches", {
      candidates: candidates.slice(0, 8).map(row => inspect(row.element, row.frame))
    }));
    const row = candidates[0], item = inspect(row.element, row.frame);
    return measured({ ok: true, document: documentId, element: item });
  }
  function deepHit(doc, x, y) {
    let hit = doc.elementFromPoint(x, y);
    for (let depth = 0; depth < 16 && hit?.shadowRoot; depth++) {
      const inner = hit.shadowRoot.elementFromPoint?.(x, y);
      if (!inner || inner === hit) break;
      hit = inner;
    }
    return hit;
  }
  function visualPoint(options = {}) {
    if(!localFrame)return fail('frame_method_invalid','Visual point routing requires a native isolated frame');
    if(options.document&&options.document!==documentId)return fail('stale_document','The point document changed');
    const point=options.point;
    if(!point||!Number.isFinite(point.x)||!Number.isFinite(point.y)||point.x<0||point.y<0||point.x>=innerWidth||point.y>=innerHeight)
      return fail('visual_point_outside_viewport','The point is outside this frame viewport');
    const hit=deepHit(document,point.x,point.y);
    if(!hit)return fail('visual_point_unavailable','The current point has no rendered target');
    if(['iframe','frame'].includes(hit.localName)){
      const owner=pinCaptureFrame(hit);if(!owner.ok)return owner;
      return{ok:true,document:documentId,frame_ref:owner.ref};
    }
    if(options.op==='key'){
      let target=hit;
      for(let depth=0;target&&target.tabIndex<0&&!target.isContentEditable&&depth<64;depth++)target=target.parentElement||target.getRootNode()?.host;
      if(!target||target===document.body||target===document.documentElement||target.tabIndex<0&&!target.isContentEditable)
        return fail('visual_focus_unavailable','Choose a visible keyboard-focusable control or use an exact DOM target');
      while(target.parentElement?.isContentEditable)target=target.parentElement;
      if(target===document.body||target===document.documentElement)return fail('visual_focus_unavailable','Choose a bounded keyboard control rather than the whole document');
      if(!visible(target)||target.matches(':disabled')||target.getAttribute('aria-disabled')==='true'||target.closest('[inert]'))
        return fail('element_disabled','The visual keyboard target is hidden, inert or disabled');
      if(['input','textarea','select'].includes(target.localName))return fail('visual_region_masked','Use an exact DOM target for form controls');
      const ref=reference(target,'f0');if(!ref)return fail('reference_limit','Too many retained element references');
      return{ok:true,document:documentId,frame_ref:null,focus_ref:ref};
    }
    return{ok:true,document:documentId,frame_ref:null};
  }
  function scrollPoint(options = {}) {
    if(!localFrame)return fail('frame_method_invalid','Scroll point routing requires a native isolated frame');
    if(options.document!==documentId)return fail('stale_document','Observe the original document before scrolling');
    const point=options.point;
    if(!point||!Number.isFinite(point.x)||!Number.isFinite(point.y)||point.x<0||point.y<0||point.x>=innerWidth||point.y>=innerHeight)
      return fail('input_coordinate_invalid','The scroll point is outside the original viewport');
    const hit=deepHit(document,point.x,point.y);
    if(!hit||!hit.isConnected||hit.ownerDocument!==document)return fail('input_target_changed','The scroll point has no original rendered target');
    const ref=reference(hit,'f0');if(!ref)return fail('reference_limit','Too many retained element references');
    if(options.ref&&options.ref!==ref)return fail('input_target_changed','The original scroll point target changed');
    if(['iframe','frame'].includes(hit.localName)){
      // Only the trusted native-frame driver may resolve this owner to its
      // exact child. Never walk contentWindow/document or guess by URL/name.
      if(options._route_child!==true)return fail('android_frames_unavailable','This scroll point needs its original native child-frame route');
      return{ok:true,document:documentId,point:{x:point.x,y:point.y},frame_ref:ref};
    }
    return{ok:true,document:documentId,point:{x:point.x,y:point.y},hit_ref:ref,...(options._route_child===true?{frame_ref:null}:{})};
  }
  function uploadInfo(options = {}) {
    const found = locate({ ...options, _upload: true }); if (!found.ok) return found;
    const element = refs.get(found.element.ref).element;
    if (element.localName !== "input" || element.type !== "file") return fail("element_not_file_input", "Choose an exact HTML file input");
    let parent=element;
    for(let depth=0;parent&&depth<256;depth++){
      if(parent.inert)return fail("element_disabled","The file input or its shadow/frame ancestor is inert");
      const doc=parent.ownerDocument;
      parent=parent.assignedSlot||parent.parentElement||parent.getRootNode()?.host||doc?.defaultView?.frameElement;
    }
    if(parent)return fail("element_ancestry_limit","Narrow the file input ancestry before acting");
    if (found.element.disabled) return fail("element_disabled", "The file input is disabled");
    return { ok: true, document: documentId, element: found.element };
  }
  // Chromium's empty DOM.setFileInputFiles is not a clear operation. Keep the
  // controlled value change and its single event pair in one isolated task.
  function clearFiles(options={}) {
    const before=uploadInfo(options);
    if(!before.ok)return{...before,error:{...before.error,effect_may_have_occurred:false}};
    const element=refs.get(before.element.ref).element;
    const result={ok:true,cleared:true,verified:true,verification:"file_list",input_method:"dom_file_clear",events_trusted:false};
    if(!element.files.length)return{...result,changed:false,dispatched:false};
    try{
      element.value="";
      if(element.files.length)throw Error("The file input did not clear");
      element.dispatchEvent(new Event("input",{bubbles:true,composed:true}));
      element.dispatchEvent(new Event("change",{bubbles:true}));
      const after=uploadInfo({document:options.document,target:{ref:before.element.ref}});
      if(!after.ok||element.files.length)throw Error("The file input changed during its handlers");
      return{...result,dispatched:true};
    }catch{return fail("action_uncertain","The file selection change was not confirmed; inspect before repeating",{effect_may_have_occurred:true});}
  }
  // Only the trusted native adapter requests a remote DOM object. Public
  // observations/results use uploadInfo and never serialize a live element.
  function uploadNode(options = {}) {
    const found = uploadInfo(options); return found.ok ? refs.get(found.element.ref).element : null;
  }
  // A native intercepted chooser supplies the actual node, never a model's
  // selector or protocol ID. Only current same-origin documents can be pinned.
  function pinUploadNode(element) {
    try {
      if (!element?.isConnected || element.localName !== "input" || element.type !== "file")
        return fail("element_not_file_input", "The picker no longer has a connected HTML file input");
      let doc = element.ownerDocument, owner = null;
      for (let depth = 0; doc !== document && depth < maxFrames; depth++) {
        if (doc.defaultView?.document !== doc) return fail("stale_frame", "The picker frame has navigated");
        const frame = doc.defaultView.frameElement;
        if (!frame?.isConnected || frame.contentDocument !== doc) return fail("frame_unsupported", "Use manual control for this picker frame");
        owner ||= frame; doc = frame.ownerDocument;
      }
      if (doc !== document) return fail("frame_unsupported", "The picker frame is outside the observable document");
      const ref = reference(element, owner ? frameIdentity(owner) : "f0");
      return ref ? uploadInfo({document:documentId,target:{ref}}) : fail("reference_limit", "Too many retained element references");
    } catch { return fail("frame_unsupported", "Use manual control for this picker frame"); }
  }
  function contains(parent, child) {
    for (let node = child, depth = 0; node && depth < 32; depth++) {
      if (parent === node || parent.contains(node)) return true;
      node = node.getRootNode()?.host;
    }
    return false;
  }
  function pointFor(element, local) {
    let doc = element.ownerDocument, x = local.x, y = local.y;
    if (!contains(element, deepHit(doc, x, y))) return null;
    for (let depth = 0; doc !== document && depth < 16; depth++) {
      const frame = doc.defaultView.frameElement;
      if (!frame?.isConnected) return null;
      for (let ancestor = frame; ancestor; ancestor = ancestor.parentElement) {
        const style = ancestor.ownerDocument.defaultView.getComputedStyle(ancestor);
        if (style.transform !== "none" || (style.zoom && Number(style.zoom) !== 1)) return { unsupported: true };
      }
      const rect = frame.getBoundingClientRect();
      x += rect.x + frame.clientLeft; y += rect.y + frame.clientTop;
      doc = frame.ownerDocument;
      if (deepHit(doc, x, y) !== frame) return null;
    }
    if (doc !== document || x < 0 || y < 0 || x >= innerWidth || y >= innerHeight) return null;
    return { x, y };
  }
  function prepare(options = {}) {
    const found = locate(options); if (!found.ok) return found;
    const row = refs.get(found.element.ref), element = row.element;
    if (found.element.disabled) return fail("element_disabled", "The target is disabled");
    if (["fill", "type"].includes(options.op)) {
      if (!found.element.editable || element.localName === "input" &&
          !["text", "search", "tel", "url", "email", "password", "number"].includes(element.type)) {
        return fail("element_not_editable", "The target does not accept text input");
      }
    }
    if (options.op === "check" && !["checkbox", "radio"].includes(found.element.role)) return fail("element_not_checkable", "The target is not a checkbox or radio input");
    if (options.op === "select" && element.localName !== "select") return fail("element_not_selectable", "The target is not an HTML select; operate a custom dropdown through its visible controls");
    if (options.scroll) element.scrollIntoView({ block: "center", inline: "center", behavior: "instant" });
    const rect = element.getBoundingClientRect();
    const viewport = element.ownerDocument.defaultView;
    const left = Math.max(0, rect.left), top = Math.max(0, rect.top);
    const right = Math.min(viewport.innerWidth, rect.right), bottom = Math.min(viewport.innerHeight, rect.bottom);
    if (right <= left || bottom <= top) return fail("element_outside_viewport", "The target is outside the viewport");
    let point;
    for (const [rx, ry] of [[0.5, 0.5], [0.25, 0.25], [0.75, 0.75]]) {
      point = pointFor(element, { x: left + (right-left)*rx, y: top + (bottom-top)*ry });
      if (point) break;
    }
    if (point?.unsupported) return fail("unsupported_geometry", "Transformed frame coordinates require visual or manual control");
    if (!point) return fail("element_obscured", "Another element covers the target");
    const current = inspect(element, row.frame);
    return { ok: true, document: documentId, element: current, point,
      geometry: [rect.x, rect.y, rect.width, rect.height, point.x, point.y] };
  }
  function verify(options = {}) {
    const found = locate(options); if (!found.ok) return found;
    const element = refs.get(found.element.ref).element;
    let matches = true;
    if (options.op === "fill") matches = (element.isContentEditable ? element.textContent : element.value) === options.text;
    if (options.op === "check") matches = element.checked === options.checked;
    const focusedIn = doc => {
      let active=doc.activeElement;
      for(let depth=0;depth<16&&active?.shadowRoot?.activeElement;depth++)active=active.shadowRoot.activeElement;
      return active;
    };
    let focused=focusedIn(element.ownerDocument)===element,doc=element.ownerDocument;
    for(let depth=0;focused&&doc!==document&&depth<16;depth++){
      const frame=doc.defaultView?.frameElement;
      if(!frame?.isConnected){focused=false;break;}
      doc=frame.ownerDocument;focused=focusedIn(doc)===frame;
    }
    if(doc!==document)focused=false;
    return { ok: true, verified: matches, document: documentId,
      focused,
      sensitive: element.localName === "input" && element.type === "password" };
  }
  function focus(options = {}) {
    const found=locate(options);if(!found.ok)return found;
    const element=refs.get(found.element.ref).element;
    if(found.element.disabled||!visible(element)||typeof element.focus!=='function')return fail('visual_focus_unavailable','The original control cannot receive focus');
    element.focus({preventScroll:true});
    const result=verify(options);return{...result,dispatched:true};
  }
  const optionDisabled = option => option.disabled || option.parentElement?.localName === "optgroup" && option.parentElement.disabled;
  function optionTarget(options) {
    const found = locate(options); if (!found.ok) return found;
    const row = refs.get(found.element.ref), element = row.element;
    if (element.localName !== "select") return fail("element_not_selectable", "Option discovery requires an HTML select");
    if (element.options.length > 256) return fail("selection_limit", "The select exceeds the 256-option inspection and mutation budget");
    return { ok:true, found, row, element, options:Array.from(element.options) };
  }
  function optionList(options = {}) {
    const target=optionTarget(options);if(!target.ok)return target;
    const offset=Number(options.offset||0),limit=Math.min(256,Math.max(1,Number(options.limit)||100));
    if(!Number.isInteger(offset)||offset<0||offset>256)return fail("arguments_invalid","Invalid option offset");
    const out={ok:true,document:documentId,ref:target.found.element.ref,frame:target.row.frame,
      multiple:target.element.multiple,option_count:target.options.length,offset,options:[],truncated:false,values_exposed:false};
    const budget=Math.min(32768,Math.max(256,Number(options.max_bytes)||16384));
    for(let index=offset;index<target.options.length&&out.options.length<limit;index++){
      const option=target.options[index],raw=option.label,label=clip(normalize(raw.slice(0,4096)),256);
      const entry={index,label:label.text,disabled:Boolean(optionDisabled(option))};
      if(label.truncated||raw.length>4096)entry.label_truncated=true;
      out.options.push(entry);
      if(new TextEncoder().encode(JSON.stringify({...out,next_offset:index+1,truncated:true})).length>budget){out.options.pop();break;}
    }
    const next=offset+out.options.length;
    if(next<target.options.length){
      if(!out.options.length)return fail("option_result_limit","Increase max_bytes to read an option within its result budget");
      out.next_offset=next;out.truncated=true;
    }
    return out;
  }
  function selectOptions(options = {}) {
    const no=(code,message)=>fail(code,message,{effect_may_have_occurred:false});
    const target=optionTarget(options);if(!target.ok)return {...target,error:{...target.error,effect_may_have_occurred:false}};
    const {element}=target,choices=options.choices,multiple=element.multiple;
    if(element.disabled||element.matches(":disabled")||element.getAttribute("aria-disabled")==="true")return no("element_disabled","The select is disabled");
    if(!Array.isArray(choices)||choices.length>32)return no("choice_invalid","Specify at most 32 exact choices");
    if(!multiple&&choices.length!==1)return no("choice_invalid","A single-select requires exactly one choice");
    const snapshot=[];let inspected=0;
    for(const option of target.options){
      const value=option.value,label=option.label;
      inspected+=value.length+label.length;
      if(value.length>4096||label.length>4096||inspected>65536)return no("selection_limit","Option metadata exceeds the bounded selection snapshot");
      snapshot.push({value,label:normalize(label),disabled:Boolean(optionDisabled(option))});
    }
    const desired=new Set();
    for(const choice of choices){
      if(!choice||Object.keys(choice).length!==1||!["label","value","index"].some(key=>Object.hasOwn(choice,key)))return no("choice_invalid","Use exactly one label, value or index for each choice");
      const matches=snapshot.map((option,index)=>({option,index})).filter(({option,index})=>
        choice.index!==undefined?index===choice.index:choice.value!==undefined?option.value===choice.value:option.label===normalize(choice.label));
      if(!matches.length)return no("option_not_found","No option matches the requested choice");
      if(matches.length!==1)return no("option_ambiguous","More than one option matches; select an exact observed index");
      const match=matches[0];if(match.option.disabled)return no("option_disabled","The requested option or its group is disabled");
      if(desired.has(match.index))return no("choice_duplicate","Multiple choices resolve to the same option");
      desired.add(match.index);
    }
    const verified=()=>element.isConnected&&element.ownerDocument===target.row.documentNode&&element.multiple===multiple&&
      !element.matches(":disabled")&&element.getAttribute("aria-disabled")!=="true"&&
      element.options.length===target.options.length&&target.options.every((option,index)=>element.options[index]===option&&
        option.value===snapshot[index].value&&option.label.length<=4096&&normalize(option.label)===snapshot[index].label&&
        Boolean(optionDisabled(option))===snapshot[index].disabled&&option.selected===desired.has(index));
    if(verified())return {ok:true,document:documentId,changed:false,dispatched:false,verified:true,input_method:"dom_select",events_trusted:false};
    // One controlled selection transaction, followed by exactly one input and
    // change. Keyboard navigation may commit unwanted intermediate choices.
    // These standard DOM events are intentionally not claimed to be trusted.
    try{
      if(element.multiple){const set=Object.getOwnPropertyDescriptor(HTMLOptionElement.prototype,"selected").set;
        for(let index=0;index<target.options.length;index++)set.call(target.options[index],desired.has(index));
      }else Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,"selectedIndex").set.call(element,[...desired][0]);
      element.dispatchEvent(new Event("input",{bubbles:true,composed:true}));
      element.dispatchEvent(new Event("change",{bubbles:true}));
      if(!verified())return fail("action_uncertain","The page changed the selection during its event handlers; inspect before repeating",{effect_may_have_occurred:true});
      return {ok:true,document:documentId,changed:true,dispatched:true,verified:true,input_method:"dom_select",events_trusted:false};
    }catch{return fail("action_uncertain","The selection or its event handlers did not finish; inspect before repeating",{effect_may_have_occurred:true});}
  }
  // Only the native owner resolver supplies an actual node. No selector or
  // page-provided ID can bind another frame's privacy mask to this reference.
  function pinCaptureFrame(element) {
    if(!localFrame||!element?.isConnected||element.ownerDocument!==document||!['iframe','frame'].includes(element.localName))
      return fail('frame_owner_unavailable','The capture owner is outside this isolated document');
    const ref=reference(element,'f0');
    return ref?{ok:true,document:documentId,ref}:fail('reference_limit','Too many retained element references');
  }
  function captureArea(options = {}) {
    if (options.document && options.document !== documentId) return fail("stale_document", "Observe the current document before capturing it");
    if(localFrame&&options._native_viewport===true){
      const v=visualViewport;
      if(!v||![v.width,v.height,v.scale,v.offsetLeft,v.offsetTop].every(Number.isFinite)||v.scale<=0||
         Math.abs(v.width-innerWidth)>1||Math.abs(v.height-innerHeight)>1||Math.abs(v.offsetLeft)>.01||Math.abs(v.offsetTop)>.01)
        return fail('unsupported_geometry','This native screenshot requires the original complete visible viewport');
    }else if (Math.abs((visualViewport?.scale || 1) - 1) > 0.001) return fail("unsupported_geometry", "Pinch-zoomed screenshots require a supported viewport scale");
    const intersect = (a, b) => { const x=Math.max(a.x,b.x),y=Math.max(a.y,b.y);return {x,y,width:Math.max(0,Math.min(a.x+a.width,b.x+b.width)-x),height:Math.max(0,Math.min(a.y+a.height,b.y+b.height)-y)}; };
    const viewport={x:0,y:0,width:innerWidth,height:innerHeight};
    function rootRect(element) {
      let doc=element.ownerDocument,box=element.getBoundingClientRect(),area={x:box.x,y:box.y,width:box.width,height:box.height};
      for(let depth=0;doc!==document&&depth<16;depth++){
        const view=doc.defaultView,frame=view?.frameElement;if(!frame?.isConnected)throw Error("Frame changed during screenshot preparation");
        for(let node=frame;node;node=node.parentElement){const style=node.ownerDocument.defaultView.getComputedStyle(node);if(style.transform!=="none"||(style.zoom&&Number(style.zoom)!==1))throw Error("Transformed frame screenshots require visual/manual control");}
        area=intersect(area,{x:0,y:0,width:view.innerWidth,height:view.innerHeight});
        const parent=frame.getBoundingClientRect();area.x+=parent.x+frame.clientLeft;area.y+=parent.y+frame.clientTop;doc=frame.ownerDocument;
      }
      if(doc!==document)throw Error("Frame nesting exceeds the screenshot budget");
      if(options.mode!=="full_page")area=intersect(area,viewport);
      area.x+=scrollX;area.y+=scrollY;return area;
    }
    let region={x:scrollX,y:scrollY,width:innerWidth,height:innerHeight};
    try {
      if(options.mode==="element"){
        const found=locate({document:documentId,target:options.target});if(!found.ok)return found;
        region=rootRect(refs.get(found.element.ref).element);
      }else if(options.mode==="full_page"){
        if(scrollX<0)return fail("unsupported_geometry", "Full-page capture of a negative horizontal scroll requires a supported layout");
        region={x:0,y:0,width:Math.max(innerWidth,document.documentElement.scrollWidth,document.body?.scrollWidth||0),
          height:Math.max(innerHeight,document.documentElement.scrollHeight,document.body?.scrollHeight||0)};
      }
      region={x:Math.floor(region.x),y:Math.floor(region.y),width:Math.ceil(region.width),height:Math.ceil(region.height)};
      const dpr=devicePixelRatio||1;
      if(region.width<=0||region.height<=0)return fail("element_outside_viewport", "The screenshot target has no visible area");
      if(region.width>32768||region.height>32768||region.width*region.height>16777216||region.width*region.height*dpr*dpr>33554432)
        return fail("capture_area_limit", "The page area exceeds the screenshot raster budget; capture a smaller region");
      const redactions=[];let nodes=0,frames=0;const started=performance.now(),maximumNodes=Math.min(6000,Math.max(1,Number(options._max_nodes)||6000));
      function mask(element,metadata={}){const area=intersect(rootRect(element),region);if(area.width&&area.height){if(redactions.length>=256)throw Error("Screenshot masking budget exceeded");redactions.push({...area,...metadata});}}
      function walk(root){
        const walker=root.ownerDocument?.createTreeWalker(root,1)||document.createTreeWalker(root,1);
        for(let element=walker.nextNode();element;element=walker.nextNode()){
          if(++nodes>maximumNodes||performance.now()-started>100)throw Error("Screenshot masking scan exceeded its budget");
          // Semantic/action visibility is not pixel privacy. Inert and
          // aria-hidden controls still paint; author CSS can override hidden.
          const style=element.ownerDocument.defaultView.getComputedStyle(element);
          if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||Number(style.opacity)===0||!element.getClientRects().length)continue;
          const tag=element.localName;
          if(tag==="textarea"||tag==="select"||element.isContentEditable||element.hasAttribute("data-mdo-private")||
            ["textbox","combobox","spinbutton"].includes(element.getAttribute("role"))||
            tag==="input"&&!["button","submit","reset","checkbox","radio","hidden","image"].includes(element.type))mask(element);
          if(element.shadowRoot)walk(element.shadowRoot);
          else if(tag.includes("-"))mask(element); // Closed custom-element internals cannot be inspected here.
          if(options._opaque_media===true&&['video','object','embed'].includes(tag))mask(element);
          if(["iframe","frame"].includes(tag)){
            if(++frames>32)throw Error("Screenshot frame budget exceeded");
            if(localFrame){const owner=pinCaptureFrame(element);if(!owner.ok)throw Error('Screenshot frame reference budget exceeded');mask(element,{frame_ref:owner.ref});continue;}
            let child=null;try{child=element.contentDocument;}catch{}
            if(child)walk(child);else mask(element);
          }
        }
      }
      walk(document);
      return {ok:true,document:documentId,region,redactions,visited:nodes,scroll:{x:scrollX,y:scrollY},viewport:{width:innerWidth,height:innerHeight},device_pixel_ratio:dpr,redaction:"form-controls-and-opaque-content"};
    } catch(error){return fail("capture_preparation_failed",String(error.message||error).slice(0,256));}
  }
  const runtime = Object.freeze({ version, documentNode: document, document: documentId, describe, observe: scan, locate, prepare, verify, focus, visualPoint, scrollPoint, optionList, selectOptions, captureArea, pinCaptureFrame, uploadInfo, uploadNode, pinUploadNode, clearFiles });
  Object.defineProperty(globalThis, marker, { value: runtime, configurable: true });
  return runtime;
}
