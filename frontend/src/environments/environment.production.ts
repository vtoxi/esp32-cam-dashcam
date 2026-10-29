export const environment = {
  production: true,
  // Deployed at carsentinal.vtoxi.com, pointed at the sibling
  // carsentinal-api.vtoxi.com subdomain reserved for the backend. Until a
  // backend is actually deployed there, every request fails with a clear
  // "cannot reach backend" toast — override via Settings (localStorage) to
  // point this build at a different backend without a rebuild.
  apiBaseUrl: 'https://carsentinal-api.vtoxi.com/api/v1',
};
