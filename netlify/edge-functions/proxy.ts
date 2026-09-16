import type { Context } from "@netlify/edge-functions";

export default async function handler(request: Request, context: Context): Promise<Response> {
  const corsHeaders: Record<string, string> = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
    "Access-Control-Allow-Headers": "*",
    "Cross-Origin-Resource-Policy": "cross-origin",
  };

  // 1. Handle preflight OPTIONS request
  if (request.method === "OPTIONS") {
    return new Response(null, {
      status: 204,
      headers: corsHeaders,
    });
  }

  // 2. Extract and validate target URL from query parameter
  const requestUrl = new URL(request.url);
  const targetUrlStr = requestUrl.searchParams.get("url");

  if (!targetUrlStr) {
    return new Response("Missing 'url' query parameter", {
      status: 400,
      headers: corsHeaders,
    });
  }

  let targetUrl: URL;
  try {
    targetUrl = new URL(targetUrlStr);
  } catch {
    return new Response("Invalid URL parameter", {
      status: 400,
      headers: corsHeaders,
    });
  }

  if (targetUrl.protocol !== "http:" && targetUrl.protocol !== "https:") {
    return new Response("Only HTTP and HTTPS protocols are supported", {
      status: 400,
      headers: corsHeaders,
    });
  }

  // 3. Fetch upstream resource with standard User-Agent
  try {
    const upstreamResponse = await fetch(targetUrl.toString(), {
      method: request.method,
      headers: {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept": "*/*",
      },
    });

    const responseHeaders = new Headers(corsHeaders);

    // 4. Forward critical upstream headers
    const forwardedHeaderNames = [
      "content-type",
      "content-length",
      "etag",
      "last-modified",
      "content-disposition",
      "cache-control",
      "accept-ranges",
    ];

    for (const name of forwardedHeaderNames) {
      const val = upstreamResponse.headers.get(name);
      if (val !== null) {
        responseHeaders.set(name, val);
      }
    }

    return new Response(upstreamResponse.body, {
      status: upstreamResponse.status,
      statusText: upstreamResponse.statusText,
      headers: responseHeaders,
    });
  } catch (err: any) {
    return new Response(`Proxy error fetching upstream resource: ${err?.message || err}`, {
      status: 502,
      headers: corsHeaders,
    });
  }
}
