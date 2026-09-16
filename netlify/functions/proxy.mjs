export const handler = async (event) => {
  const corsHeaders = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
    "Access-Control-Allow-Headers": "*",
    "Cross-Origin-Resource-Policy": "cross-origin",
  };

  // Handle CORS preflight
  if (event.httpMethod === "OPTIONS") {
    return {
      statusCode: 204,
      headers: corsHeaders,
      body: "",
    };
  }

  const targetUrl = event.queryStringParameters?.url;
  if (!targetUrl) {
    return {
      statusCode: 400,
      headers: corsHeaders,
      body: "Missing 'url' query parameter",
    };
  }

  try {
    const upstream = await fetch(targetUrl, {
      headers: { "User-Agent": "VESC-Tool-Web/1.0" },
      redirect: "follow",
    });

    const arrayBuffer = await upstream.arrayBuffer();
    const buffer = Buffer.from(arrayBuffer);

    // Forward upstream headers and add CORS/CORP
    const responseHeaders = { ...corsHeaders };
    const copyHeaders = ["content-type", "content-length", "etag", "last-modified", "content-disposition"];
    for (const h of copyHeaders) {
      const val = upstream.headers.get(h);
      if (val) responseHeaders[h] = val;
    }

    return {
      statusCode: upstream.status,
      headers: responseHeaders,
      body: buffer.toString("base64"),
      isBase64Encoded: true,
    };
  } catch (err) {
    return {
      statusCode: 502,
      headers: corsHeaders,
      body: `Proxy error: ${err.message}`,
    };
  }
};
