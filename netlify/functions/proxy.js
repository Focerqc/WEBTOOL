exports.handler = async (event) => {
  const corsHeaders = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
    "Access-Control-Allow-Headers": "*",
    "Cross-Origin-Resource-Policy": "cross-origin",
  };

  if (event.httpMethod === "OPTIONS") {
    return {
      statusCode: 204,
      headers: corsHeaders,
      body: "",
    };
  }

  // Robustly extract target URL from query parameters or raw query string
  let targetUrl = event.queryStringParameters && (event.queryStringParameters.url || event.queryStringParameters.URL);
  if (!targetUrl && event.rawQuery) {
    const match = event.rawQuery.match(/(?:^|[?&])url=([^&]+)/i);
    if (match) {
      try {
        targetUrl = decodeURIComponent(match[1]);
      } catch {
        targetUrl = match[1];
      }
    }
  }

  if (!targetUrl) {
    return {
      statusCode: 400,
      headers: { ...corsHeaders, "Content-Type": "text/plain" },
      body: "Missing 'url' query parameter",
    };
  }

  try {
    const upstream = await fetch(targetUrl, {
      headers: {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept": "*/*",
      },
      redirect: "follow",
    });

    if (!upstream.ok) {
      const errBody = await upstream.text().catch(() => "");
      console.warn(`[proxy.js] Upstream returned HTTP ${upstream.status} for ${targetUrl}: ${errBody.slice(0, 200)}`);
      return {
        statusCode: upstream.status,
        headers: { ...corsHeaders, "Content-Type": "text/plain" },
        body: `Upstream error ${upstream.status}: ${errBody.slice(0, 500)}`,
        isBase64Encoded: false,
      };
    }

    const arrayBuffer = await upstream.arrayBuffer();
    const buffer = Buffer.from(arrayBuffer);

    // Prepare response headers
    // NOTE: DO NOT copy 'content-length'. Netlify's edge layer automatically compresses
    // or chunks responses. Manually setting Content-Length causes a header-body length
    // mismatch during CDN compression, resulting in an HTTP 500 Internal Server Error.
    const contentType = upstream.headers.get("content-type") || "application/octet-stream";
    const responseHeaders = {
      ...corsHeaders,
      "Content-Type": contentType,
      "Cache-Control": upstream.headers.get("cache-control") || "public, max-age=3600",
    };

    const safeForwardHeaders = ["etag", "last-modified", "content-disposition", "accept-ranges"];
    for (const h of safeForwardHeaders) {
      const val = upstream.headers.get(h);
      if (val) responseHeaders[h] = val;
    }

    return {
      statusCode: 200,
      headers: responseHeaders,
      body: buffer.toString("base64"),
      isBase64Encoded: true,
    };
  } catch (err) {
    console.error("[proxy.js] Exception fetching target URL:", targetUrl, err);
    return {
      statusCode: 502,
      headers: { ...corsHeaders, "Content-Type": "text/plain" },
      body: "Proxy error: " + (err.message || String(err)),
    };
  }
};
