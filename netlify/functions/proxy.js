const http = require("http");
const https = require("https");
const { URL } = require("url");

function fetchUrl(targetUrl, maxRedirects = 5) {
  return new Promise((resolve, reject) => {
    if (maxRedirects < 0) {
      return reject(new Error("Too many redirects"));
    }

    let parsedUrl;
    try {
      parsedUrl = new URL(targetUrl);
    } catch (e) {
      return reject(new Error("Invalid URL: " + targetUrl));
    }

    const client = parsedUrl.protocol === "https:" ? https : http;
    const req = client.get(
      parsedUrl,
      {
        headers: {
          "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
          Accept: "*/*",
        },
        timeout: 30000,
      },
      (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          const redirectUrl = new URL(res.headers.location, parsedUrl).toString();
          return fetchUrl(redirectUrl, maxRedirects - 1).then(resolve).catch(reject);
        }

        const chunks = [];
        res.on("data", (chunk) => chunks.push(chunk));
        res.on("end", () => {
          resolve({
            statusCode: res.statusCode || 200,
            headers: res.headers,
            data: Buffer.concat(chunks),
          });
        });
      }
    );

    req.on("error", reject);
    req.on("timeout", () => {
      req.destroy();
      reject(new Error("Upstream request timed out"));
    });
  });
}

const handler = async (event, context) => {
  console.log("[proxy.js] Invoked method:", event ? event.httpMethod : "UNKNOWN");

  const corsHeaders = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
    "Access-Control-Allow-Headers": "*",
    "Cross-Origin-Resource-Policy": "cross-origin",
  };

  if (event && event.httpMethod === "OPTIONS") {
    return {
      statusCode: 204,
      headers: corsHeaders,
      body: "",
    };
  }

  let targetUrl = event && event.queryStringParameters && (event.queryStringParameters.url || event.queryStringParameters.URL);
  if (!targetUrl && event && event.rawQuery) {
    const match = event.rawQuery.match(/(?:^|[?&])url=([^&]+)/i);
    if (match) {
      try {
        targetUrl = decodeURIComponent(match[1]);
      } catch (e) {
        targetUrl = match[1];
      }
    }
  }

  if (!targetUrl) {
    console.warn("[proxy.js] Missing 'url' parameter");
    return {
      statusCode: 400,
      headers: { ...corsHeaders, "Content-Type": "text/plain" },
      body: "Missing 'url' query parameter",
    };
  }

  console.log("[proxy.js] Fetching upstream target:", targetUrl);

  try {
    const upstream = await fetchUrl(targetUrl);
    console.log(`[proxy.js] Upstream responded HTTP ${upstream.statusCode}, size: ${upstream.data.length} bytes`);

    if (upstream.statusCode >= 400) {
      return {
        statusCode: upstream.statusCode,
        headers: { ...corsHeaders, "Content-Type": "text/plain" },
        body: `Upstream error ${upstream.statusCode}: ${upstream.data.slice(0, 500).toString("utf-8")}`,
        isBase64Encoded: false,
      };
    }

    const contentType = upstream.headers["content-type"] || "application/octet-stream";
    const responseHeaders = {
      ...corsHeaders,
      "Content-Type": contentType,
      "Cache-Control": upstream.headers["cache-control"] || "public, max-age=3600",
    };

    const safeForwardHeaders = ["etag", "last-modified", "content-disposition", "accept-ranges"];
    for (const h of safeForwardHeaders) {
      const val = upstream.headers[h];
      if (val) responseHeaders[h] = val;
    }

    return {
      statusCode: 200,
      headers: responseHeaders,
      body: upstream.data.toString("base64"),
      isBase64Encoded: true,
    };
  } catch (err) {
    console.error("[proxy.js] Error fetching upstream URL:", targetUrl, err);
    return {
      statusCode: 502,
      headers: { ...corsHeaders, "Content-Type": "text/plain" },
      body: "Proxy error: " + (err.message || String(err)),
    };
  }
};

exports.handler = handler;
module.exports = { handler };
module.exports.handler = handler;
