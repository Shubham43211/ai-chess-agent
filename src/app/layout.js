export const metadata = {
    title: 'AlphaChess | C++ Wasm Engine',
    description: 'High-performance chess engine powered by Bitboards and WebAssembly',
  };
  
  export default function RootLayout({ children }) {
    return (
      <html lang="en">
        <head>
          <script src="https://cdn.tailwindcss.com"></script>
        </head>
        <body style={{ margin: 0, padding: 0, backgroundColor: '#161512' }}>
          {children}
        </body>
      </html>
    );
  }